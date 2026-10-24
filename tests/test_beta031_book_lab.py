# Copyright 2026 Jamison A. Drapeau
"""Local, dependency-free tests for the Beta 0.3.1 Book lab helpers."""
import importlib.util
import json
import socket
import struct
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("beta031_book_lab", ROOT / "scripts/beta031_book_lab.py")
lab = importlib.util.module_from_spec(spec)
spec.loader.exec_module(lab)


def fixture_pcap(path, packets=1, port=8080):
    blob = bytearray(struct.pack("<IHHiiII", 0xA1B2C3D4, 2, 4, 0, 0, 65535, 1))
    for _ in range(packets):
        ethernet = b"\xaa" * 6 + b"\xbb" * 6 + b"\x08\x00"
        ipv4 = (b"\x45\x00" + struct.pack(">H", 40) + b"\x00\x00\x00\x00"
                + b"\x40\x06\x00\x00" + b"\xc0\x00\x02\x01"
                + b"\xc0\x00\x02\x02")
        tcp = struct.pack(">HH", 40000, port) + b"\x00" * 16
        frame = ethernet + ipv4 + tcp
        blob.extend(struct.pack("<IIII", 1, 2, len(frame), len(frame)))
        blob.extend(frame)
    path.write_bytes(blob)


def fixture_out(path, fields):
    lines = ["KAMINOWAKU_BOOK_OUT\t1", "BOOK\t" + path.stem]
    for key, value in fields.items():
        lines.append("FIELD\tdefault\t%s\t%s" % (key, value.encode("utf-8").hex()))
    path.write_text("\n".join(lines) + "\n", encoding="ascii")


class TestLabServers(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.servers = lab.start_servers("127.0.0.1", 0, 0, 0)
        cls.ports = [server.server_address[1] for server in cls.servers]

    @classmethod
    def tearDownClass(cls):
        lab.stop_servers(cls.servers)

    def request(self, port, path):
        with socket.create_connection(("127.0.0.1", port), timeout=3) as client:
            client.settimeout(3)
            client.sendall(("GET %s HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n" % path).encode())
            data = bytearray()
            while True:
                chunk = client.recv(8192)
                if not chunk:
                    break
                data.extend(chunk)
            return bytes(data)

    def test_http_port_and_path(self):
        response = self.request(self.ports[1], "/status")
        self.assertIn(b"HTTP/1.1 200 OK", response)
        self.assertIn(b"KamiLab 0.3.1: HTTP fixture healthy", response)
        self.assertIn(b"HTTP/1.1 404 Not Found", self.request(self.ports[1], "/not-status"))

    def test_gitlab_public_fingerprint(self):
        response = self.request(self.ports[2], "/users/sign_in")
        self.assertIn(b"HTTP/1.1 200 OK", response)
        self.assertIn(b"X-GitLab-Meta: kaminowaku-lab", response)
        self.assertIn(b"X-GitLab-Version: 19.5.0", response)

    def test_ssh_identification_and_kexinit(self):
        with socket.create_connection(("127.0.0.1", self.ports[0]), timeout=3) as client:
            client.settimeout(3)
            self.assertEqual(lab.recv_until(client, b"\n"), b"SSH-2.0-KamiLab_031\r\n")
            client.sendall(b"SSH-2.0-Kaminowaku_Test\r\n")
            client.sendall(struct.pack(">I", 6) + b"\x05\x14" + b"\x00" * 4)
            raw = b""
            while len(raw) < 4:
                raw += client.recv(4 - len(raw))
            length = struct.unpack(">I", raw)[0]
            data = b""
            while len(data) < length:
                data += client.recv(length - len(data))
            self.assertEqual(data[1], 20)  # SSH_MSG_KEXINIT
            self.assertEqual((4 + length) % 8, 0)


class TestEvidence(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="kami-beta031-")
        self.target = Path(self.temp.name)
        self.params = {"ssh_port": 2222, "web_port": 8080, "gitlab_port": 8081}
        self.fields = {
            "ssh-kex": {"PORT": "2222", "BANNER": "SSH-2.0-KamiLab_031"},
            "web-enum": {"PORT": "8080", "SCHEME": "http", "STATUS": "200"},
            "cve-2026-110264": {
                "PORT": "8081", "CVE": "CVE-2026-110264", "PRODUCT": "GITLAB",
                "FINGERPRINT": "GITLAB_RESPONSE_HEADER", "RELEASE-STATUS": "AFFECTED_RELEASE",
            },
        }
        for name, fields in self.fields.items():
            fixture_out(self.target / (name + ".out"), fields)
            ports = {"ssh-kex": 2222, "web-enum": 8080, "cve-2026-110264": 8081}
            fixture_pcap(self.target / (name + "-TID-123.pcap"), port=ports[name])

    def tearDown(self):
        self.temp.cleanup()

    def test_positive_outputs_and_pcaps(self):
        lab.verify_target(self.target, self.params)
        self.assertEqual(lab.check_pcap(self.target / "web-enum-TID-123.pcap"), 1)

    def test_repeated_field_names_are_valid(self):
        output = self.target / "ssh-kex.out"
        with output.open("a", encoding="ascii") as stream:
            stream.write("FIELD\tdefault\tKEX\t637572766532353531392d736861323536\n")
            stream.write("FIELD\tdefault\tKEX\t6469666669652d68656c6c6d616e\n")
            stream.write("FIELD\tdefault\tHOSTKEY\t7373682d65643235353139\n")
        decoded = lab.read_book_output(output)
        self.assertEqual(len(decoded["KEX"]), 2)
        self.assertEqual(decoded["HOSTKEY"], ["ssh-ed25519"])
        lab.verify_target(self.target, self.params)

    def test_wrong_tcp_port_rejected(self):
        fixture_pcap(self.target / "web-enum-TID-123.pcap", port=9999)
        with self.assertRaisesRegex(ValueError, "no captured TCP packet"):
            lab.verify_target(self.target, self.params)

    def test_raw_ipv4_tcp_port(self):
        # Native captures may use raw IPv4 rather than Ethernet.
        frame = (self.target / "web-enum-TID-123.pcap").read_bytes()[40:]
        self.assertIn(8080, lab.pcap_tcp_ports(frame[14:], 101))

    def test_malformed_pcap_fails(self):
        (self.target / "web-enum-TID-123.pcap").write_bytes(b"\xd4\xc3\xb2\xa1")
        with self.assertRaises(ValueError):
            lab.verify_target(self.target, self.params)

    def test_snapshot_and_bail(self):
        snapshot = self.target / "snapshot.json"
        lab.snapshot(self.target, "web-enum", snapshot)
        fixture_pcap(self.target / "web-enum-TID-124.pcap", packets=0)
        lab.verify_bail(self.target, snapshot)
        (self.target / "web-enum.out").write_text("tampered\n")
        with self.assertRaises(ValueError):
            lab.verify_bail(self.target, snapshot)

    def test_missing_bail_capture_fails(self):
        snapshot = self.target / "snapshot.json"
        lab.snapshot(self.target, "web-enum", snapshot)
        with self.assertRaises(ValueError):
            lab.verify_bail(self.target, snapshot)


if __name__ == "__main__":
    unittest.main()
