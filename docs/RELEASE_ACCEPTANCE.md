# Kaminowaku 0.3.1 — Release acceptance

**Status: operator acceptance pending.** CI builds the native Linux and FreeBSD 0.3.1 scanner and tests Book arguments, Lua execution/help, and NOSIX L3/port-result invariants. Real network traffic, PCAP evidence, target storage, TUI and terminal behavior must still be tested against **owned or explicitly authorized** services.

This is a durable operator checklist, not a temporary test runner.

## 1. Release identity and build

From a clean `beta-0.3.1` source checkout on each supported OS:

```sh
sh scripts/check-release-versions.sh
./install.sh check BUILD=release --offline
./install.sh all BUILD=release --offline
```

Confirm the compiled binary is **Kaminowaku 0.3.1**, with matching **NOSIX ABI 1.5.0** and pinned **OpenSSL 3.5.9**. The source directory is a clean release tree; `.STAGE/` is testing/build output, not release content.

**Disk-space note:** check `df -h` before extracting the source on small Kali VMs. The portable source package includes the OpenSSL archive and both Linux/FreeBSD native static-library sets; keeping separate testing and release copies requires additional free space. Do not delete required bundled native files to work around insufficient storage.

## 2. Book help without a project

Start Kaminowaku as required for your lab and, before loading a project, run:

```text
book
book --help
book ssh-kex --help
book web-enum --help
book cve-2026-110264 --help
```

Each must display the correct usage and option descriptions without running a Book, sending packets, creating a PCAP, modifying `.out`, or requiring project context. The `b` alias must resolve to `book`.

Expect `ssh-kex` to describe `--port` (22); `web-enum` to describe `--port` (443), `--path` (/), and `--tls` (true); and the GitLab Book to describe `--port` (80), `--path` (/users/sign_in), `--tls` (false), and `--timeout` (active profile/native default). `--help` is reserved, valueless and cannot be combined with other flags.

## 3. Authorized live service checks

With the NOSIX interface/profile configured for a **reachable owned target**, create/load a project, add/select that target, and invoke real services with nondefault settings, for example:

```text
book ssh-kex --port 2222
book web-enum --port 8080 --tls false --path /status
```

Only if an authorized GitLab server or controlled GitLab-like endpoint is available, test the **non-exploitative**, unauthenticated CVE Book:

```text
book cve-2026-110264 --port 8081 --tls false --path /users/sign_in
```

Replace the examples with actual reachable lab service ports. A version match is **not proof of binary vulnerability**. `--timeout N` must not exceed the active profile limit; an omitted timeout uses the native/profile default.

For every completed Book, inspect `~/.kaminowaku/projects/<project>/<TID>/` and verify the `BOOKNAME.out` semantic fields and the time-stamped PCAP(s). Inspect packet records with your normal PCAP tooling if available; the requested TCP port must appear and a completed network transaction must not be supported by a header-only PCAP. Account for mixed Ethernet/raw-IPv4 link types and alternate RX files.

## 4. Persistence, errors, and BAIL

After a successful Book, record its `.out` checksum and existing PCAP names. Trigger a deliberate failure or BAIL using a controlled unreachable service, and confirm that the previous successful `.out` remains byte-identical while the **executed** Book session retains a new PCAP, even if early BAIL produces only a header. Invalid CLI syntax and `--help` must fail or return **before** capture creation.

Verify that project/target display shows the expected stored results, built-in results precede Book records, and native TX/RX accounting reflects real managed network I/O. Repeat the critical operations on both Linux and FreeBSD.

## 5. Release decision

The release gate closes only after CI, both native builds and operator-run live acceptance succeed. No live-traffic pass is claimed by this document. Do not merge into `main` before sign-off.
