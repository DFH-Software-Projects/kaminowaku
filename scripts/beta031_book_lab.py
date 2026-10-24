#!/usr/bin/env python3
"""Controlled Beta 0.3.1 Book lab: SSH/HTTP fixtures and evidence verification.

Standard-library only. No exploit payloads, credentials or external targets.
The server binds to 127.0.0.1 unless an explicit --bind is provided.
"""
import argparse
import hashlib
import json
import socket
import socketserver
import struct
import sys
from pathlib import Path


class LabServer(socketserver.ThreadingMixIn, socketserver.TCPServer):
    allow_reuse_address = True
    daemon_threads = True


def recv_until(sock, marker, limit=8192):
    data = bytearray()
    while marker not in data:
        chunk = sock.recv(min(4096, limit - len(data)))
        if not chunk:
            break
        data.extend(chunk)
        if len(data) >= limit:
            break
    return bytes(data)


def ssh_packet():
    # RFC 4253 SSH_MSG_KEXINIT (20): cookie + ten name-lists + trailer.
    names = [
        b"curve25519-sha256", b"ssh-ed25519",
        b"aes128-ctr", b"aes128-ctr",
        b"hmac-sha2-256", b"hmac-sha2-256",
        b"none", b"none", b"", b""
    ]
    payload = bytearray(b"\x14" + bytes(range(16)))
    for entry in names:
        payload.extend(struct.pack(">I", len(entry)))
        payload.extend(entry)
    payload.extend(b"\x00\x00\x00\x00\x00")
    padding = 4
    while (4 + 1 + len(payload) + padding) % 8:
        padding += 1
    length = 1 + len(payload) + padding
    return struct.pack(">I", length) + bytes((padding,)) + payload + bytes(padding)


class SSHHandler(socketserver.BaseRequestHandler):
    def handle(self):
        self.request.settimeout(6)
        try:
            self.request.sendall(b"SSH-2.0-KamiLab_031\r\n")
            ident = recv_until(self.request, b"\n", limit=1024)
            if not ident.startswith(b"SSH-2.0-"):
                return
            # The Book sends a client SSH_MSG_KEXINIT after its identification.
            header = self._exact(4)
            if not header:
                return
            length = struct.unpack(">I", header)[0]
            if length < 6 or length > 35000:
                return
            if not self._exact(length):
                return
            self.request.sendall(ssh_packet())
            print("SSH: identification + KEXINIT responded", flush=True)
        except (OSError, TimeoutError):
            return

    def _exact(self, length):
        received = bytearray()
        while len(received) < length:
            chunk = self.request.recv(length - len(received))
            if not chunk:
                return None
            received.extend(chunk)
        return bytes(received)


class HTTPHandler(socketserver.BaseRequestHandler):
    def handle(self):
        self.request.settimeout(6)
        try:
            request = recv_until(self.request, b"\r\n\r\n")
            parts = request.split(b"\r\n", 1)[0].decode("ascii", "replace").split(" ")
            if len(parts) < 2:
                return
            method, path = parts[:2]
            is_gitlab = self.server.lab_kind == "gitlab"
            expected = "/users/sign_in" if is_gitlab else "/status"
            status = 200 if method == "GET" and path == expected else 404
            if is_gitlab:
                body = b"<html><title>GitLab</title><body>GitLab Community Edition 19.5.0 - Sign in /users/sign_in</body></html>"
            else:
                body = b"KamiLab 0.3.1: HTTP fixture healthy\n"
            headers = [
                "HTTP/1.1 %d %s" % (status, "OK" if status == 200 else "Not Found"),
                "Server: KamiLab/0.3.1",
                "Content-Type: text/html" if is_gitlab else "Content-Type: text/plain",
                "Content-Length: %d" % len(body),
                "Connection: close",
            ]
            if is_gitlab:
                headers.extend(["X-GitLab-Meta: kaminowaku-lab", "X-GitLab-Version: 19.5.0"])
            payload = ("\r\n".join(headers) + "\r\n\r\n").encode("ascii") + body
            self.request.sendall(payload)
            print("%s: %s %s => %d" % (self.server.lab_kind.upper(), method, path, status), flush=True)
        except (OSError, TimeoutError):
            return


def start_servers(bind, ssh_port, web_port, gitlab_port):
    import threading
    servers = []
    try:
        for kind, port, handler in (
            ("ssh", ssh_port, SSHHandler),
            ("web", web_port, HTTPHandler),
            ("gitlab", gitlab_port, HTTPHandler),
        ):
            server = LabServer((bind, port), handler)
            server.lab_kind = kind
            servers.append(server)
        for server in servers:
            threading.Thread(target=server.serve_forever, daemon=True).start()
        return servers
    except BaseException:
        for server in servers:
            server.server_close()
        raise


def stop_servers(servers):
    for server in servers:
        server.shutdown()
        server.server_close()


def read_book_output(path):
    lines = path.read_text(encoding="ascii").splitlines()
    if (len(lines) < 2 or lines[0] != "KAMINOWAKU_BOOK_OUT\t1"
            or lines[1] != "BOOK\t" + path.stem):
        raise ValueError("invalid BOOK identity/header in %s" % path)
    fields = {}
    for line in lines[2:]:
        parts = line.split("\t")
        if len(parts) != 4 or parts[0] != "FIELD":
            raise ValueError("invalid FIELD line in %s" % path)
        _, _color, key, encoded = parts
        if not key or key in fields:
            raise ValueError("invalid or duplicated FIELD in %s" % path)
        fields[key] = bytes.fromhex(encoded).decode("utf-8", "replace")
    return fields


def check_pcap(path, require_packets=True):
    raw = path.read_bytes()
    if len(raw) < 24:
        raise ValueError("%s: truncated PCAP header" % path)
    magic = raw[:4]
    if magic in (b"\xd4\xc3\xb2\xa1", b"\x4d\x3c\xb2\xa1"):
        endian = "<"
    elif magic in (b"\xa1\xb2\xc3\xd4", b"\xa1\xb2\x3c\x4d"):
        endian = ">"
    else:
        raise ValueError("%s: unsupported PCAP magic" % path)
    major, minor, _, _, snaplen, link = struct.unpack_from(endian + "HHiiII", raw, 4)
    if (major, minor) != (2, 4) or snaplen == 0 or link not in (1, 101):
        raise ValueError("%s: invalid PCAP global header" % path)
    packets = 0
    offset = 24
    while offset < len(raw):
        if len(raw) - offset < 16:
            raise ValueError("%s: truncated PCAP packet header" % path)
        _ts_sec, _ts_subsec, captured, original = struct.unpack_from(endian + "IIII", raw, offset)
        offset += 16
        if captured > snaplen or captured > original or captured > len(raw) - offset:
            raise ValueError("%s: invalid captured packet length" % path)
        offset += captured
        packets += 1
    if require_packets and packets == 0:
        raise ValueError("%s: header-only PCAP" % path)
    return packets


def pcaps(target_dir, book):
    return sorted(target_dir.glob(book + "-*.pcap"), key=lambda item: item.stat().st_mtime_ns)


def verify_target(target_dir, ports):
    expected = {
        "ssh-kex": {"PORT": str(ports["ssh_port"]), "BANNER": "SSH-2.0-KamiLab_031"},
        "web-enum": {"PORT": str(ports["web_port"]), "SCHEME": "http", "STATUS": "200"},
        "cve-2026-110264": {
            "PORT": str(ports["gitlab_port"]), "CVE": "CVE-2026-110264",
            "PRODUCT": "GITLAB", "FINGERPRINT": "GITLAB_RESPONSE_HEADER",
            "RELEASE-STATUS": "AFFECTED_RELEASE",
        },
    }
    for book, fields in expected.items():
        output = target_dir / (book + ".out")
        if not output.is_file():
            raise ValueError("missing Book output: %s" % output)
        saved = read_book_output(output)
        for key, wanted in fields.items():
            if saved.get(key) != wanted:
                raise ValueError("%s: %s expected %r, observed %r" % (book, key, wanted, saved.get(key)))
        captures = pcaps(target_dir, book)
        if not captures:
            raise ValueError("missing PCAP for %s" % book)
        packet_count = check_pcap(captures[-1], require_packets=True)
        print("PASS %-19s PORT=%-5s PCAP=%s packets=%d" %
              (book, fields["PORT"], captures[-1].name, packet_count))


def snapshot(target_dir, book, destination):
    output = target_dir / (book + ".out")
    if not output.is_file():
        raise ValueError("missing output for snapshot: %s" % output)
    data = {
        "book": book,
        "target_dir": str(target_dir.resolve()),
        "sha256": hashlib.sha256(output.read_bytes()).hexdigest(),
        "pcaps": [path.name for path in pcaps(target_dir, book)],
    }
    destination.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
    print("Snapshot saved; now run a Book failure/BAIL on this target.")


def verify_bail(target_dir, snapshot_file):
    data = json.loads(snapshot_file.read_text(encoding="utf-8"))
    if str(target_dir.resolve()) != data["target_dir"]:
        raise ValueError("snapshot belongs to a different target directory")
    book = data["book"]
    output = target_dir / (book + ".out")
    if hashlib.sha256(output.read_bytes()).hexdigest() != data["sha256"]:
        raise ValueError("BAIL overwrote previous durable Book output")
    captures = pcaps(target_dir, book)
    fresh = [path for path in captures if path.name not in data["pcaps"]]
    if not fresh:
        raise ValueError("BAIL did not retain a fresh PCAP")
    for path in fresh:
        check_pcap(path, require_packets=False)
    print("PASS BAIL: prior .out unchanged and %d new PCAP(s) retained" % len(fresh))


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    subs = parser.add_subparsers(dest="command", required=True)
    serve = subs.add_parser("serve", help="start three authorized lab fixtures")
    serve.add_argument("--bind", default="127.0.0.1")
    for name, default in (("ssh", 2222), ("web", 8080), ("gitlab", 8081)):
        serve.add_argument("--" + name + "-port", type=int, default=default)
    verify = subs.add_parser("verify", help="verify real saved .out and PCAP artifacts")
    verify.add_argument("--target-dir", type=Path, required=True)
    for name, default in (("ssh", 2222), ("web", 8080), ("gitlab", 8081)):
        verify.add_argument("--" + name + "-port", type=int, default=default)
    snap = subs.add_parser("snapshot", help="capture the output digest before intentional BAIL")
    snap.add_argument("--target-dir", type=Path, required=True)
    snap.add_argument("--book", default="web-enum")
    snap.add_argument("--file", type=Path, required=True)
    bail = subs.add_parser("verify-bail", help="check output preservation and retained PCAP")
    bail.add_argument("--target-dir", type=Path, required=True)
    bail.add_argument("--file", type=Path, required=True)
    return parser.parse_args()


def main():
    args = parse_args()
    try:
        if args.command == "serve":
            values = [args.ssh_port, args.web_port, args.gitlab_port]
            if not all(1 <= p <= 65535 for p in values) or len(set(values)) != 3:
                raise ValueError("ports must be unique integers in 1..65535")
            servers = start_servers(args.bind, *values)
            print("Bound to %s: SSH=%d HTTP=%d GitLab=%d" %
                  (args.bind, *values), flush=True)
            print("Only test from systems you control; Ctrl-C stops fixtures.", flush=True)
            try:
                import time
                while True:
                    time.sleep(1)
            except KeyboardInterrupt:
                pass
            finally:
                stop_servers(servers)
        elif args.command == "verify":
            verify_target(args.target_dir, vars(args))
        elif args.command == "snapshot":
            snapshot(args.target_dir, args.book, args.file)
        else:
            verify_bail(args.target_dir, args.file)
    except (ValueError, OSError) as exc:
        print("FAIL:", exc, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
