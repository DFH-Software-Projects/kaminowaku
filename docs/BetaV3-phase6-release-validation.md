# BetaV3 — Phase 6 release-validation handoff

**State:** Integration and source checks finalized. **Native runtime/integration testing is intentionally pending operator execution.** The owner will manually merge the tested branches to `main`; this phase does not merge, tag, publish a release, or overwrite packaged proprietary binaries with untested artifacts.

## Branch dependencies

| Repository | Branch | Contract |
| --- | --- | --- |
| `DFH-Software-Projects/NOSIX` (private) | `BetaV3` | ABI 1.5, Linux/FreeBSD L3 IPv4 implementation |
| `DFH-Software-Projects/kaminowaku` | `BetaV3` | NOSIX ABI 1.5 headers, L3 RX/PCAP support, result mapping, `targets del -n` |

Do not merge one repository's branch without checking the other branch's ABI compatibility.

## Before testing: native ABI staging

The **tracked** Kaminowaku repository initially still carries original NOSIX 1.4 native shared libraries. Its checked-in public header was updated to 1.5 for Phase 4 source compilation. This deliberate staging state must not be confused with a usable BetaV3 deployment.

From the Kaminowaku repository, run:

```sh
sh scripts/betav3-preflight.sh --staging
```

This validates the header, licensing/manifest presence and both platform metadata; it permits the legacy 1.4 libraries **only for source/CI staging**.

Package NOSIX/BetaV3 **natively and independently on Linux and FreeBSD**, including:

- `libs/nosix/include/nosix.h`, `nosix_poll.h`, `nosix_datagram.h` from the matching NOSIX branch;
- `libs/nosix/linux/lib/libnosix.so.1.5.0`;
- `libs/nosix/freebsd/lib/libnosix.so.1.5.0`;
- each platform's `abi.env` with `REAL_NAME=libnosix.so.1.5.0`, `SONAME_NAME=libnosix.so.1`, `LINKER_NAME=libnosix.so`, and `ARCH=amd64`;
- the NOSIX license and an accurate rebuilt manifest (including source revision/packaging provenance).

The binary libraries must be **actual native builds**; do not rename a 1.4 library to 1.5.0. Kaminowaku should consume the native ABI, never NOSIX private sources.

Once the deployment script has assembled the replacement package, run:

```sh
sh scripts/betav3-preflight.sh --release
```

A successful metadata preflight does **not** verify the actual linked ABI or scanner behavior. Follow it with the existing `make` / `install.sh check` linker tests on each OS. The Phase 4 Makefile/installer rejects a stale 1.4 packaged library. Run all operator tests below **before** manually merging.

## Operator acceptance matrix

| Scenario | Required observation |
| --- | --- |
| Linux Ethernet IPv4 ICMP/TCP/UDP | Unchanged L2 TX, ARP, RX, notices and PCAP Ethernet DLT 1 |
| Linux OpenVPN `tun0` ICMPv4 | NOSIX sends raw IPv4 on selected TUN; matching Echo Reply captured and reported |
| Linux TUN TCP and UDP scans | Raw IPv4 TX, response matching and DLT 101 evidence; correct counters |
| Linux TUN DNS resolution | DNS UDP reply parsed from raw IPv4, not an assumed 14-byte Ethernet frame |
| FreeBSD Ethernet regression | Ethernet BPF TX/RX unchanged |
| FreeBSD TUN or supported BPF L3 interface | IPv4 TX/RX through supported DLT, correct DLT 101 evidence and counters |
| FreeBSD `DLT_RAW` and `DLT_NULL` | Normalized IPv4 packet body; DLT_NULL family prefix not exposed to scanner |
| Explicit TX interface/source/gateway | Chosen interface respected; incompatible route or gateway returns route error |
| Ethernet next-hop failure | `NOSIX_ERR_NEIGHBOR`, no automatic raw-IP retry |
| Attempted L3 write failure | `NOSIX_ERR_L3_FALLBACK_FAILED (-11)`; ICMPv4 displays `L3_FALLBACK` |
| Successful L3 TX without response | Timeout, not L3 fallback error |
| Mixed RX/TX linktypes | RX matches and persists in a separate sibling PCAP with correct linktype |
| Batch scanner | Auto-interface locking and RX accounting still function |
| `targets del -n` | Only exact L2-neighbor / L3-send failures on all configured families, with no response evidence, are pruned |
| Old projects and saved targets | Existing target layout, scan version and previous results preserved |
| Linux and FreeBSD deploy/install | Links and executes against the matching native NOSIX 1.5 shared library |

**Native commands (run on authorized test networks only):**

```sh
# NOSIX/BetaV3 source on each platform
make clean
make
make test
# Run on the VPN node; substitute your permitted ICMP-responding test address.
make l3-test INTERFACE=tun0 TARGET=<target-ipv4>

# Kaminowaku/BetaV3 after both ABI packages are refreshed
sh scripts/betav3-preflight.sh --release
./install.sh check --online
```

The final `--online` check uses OS-managed OpenSSL; use the existing offline mode if the test host has matching vendored OpenSSL libraries. Ensure `tun0` exists, has an IPv4 address, and the route to the target is actually through that tunnel. FreeBSD names/devices may differ.

## Evidence to collect

- Git commit SHA for **both** BetaV3 branches.
- NOSIX versioned ABI binaries and platform-specific build manifests/checksums.
- Native Linux and FreeBSD build/test output; VPN route and interface information.
- At least one successful ICMP, TCP, UDP and DNS tunnel trace with correct PCAP DLT.
- Deliberately induced L3 send failure, normal timeout and Ethernet neighbor failure records.
- `targets del -n` test on disposable targets only (it permanently deletes matching project entries).
- TX/RX counter samples, and proof that previously observed targets were preserved.

## Limitations that must remain visible

- Automated GitHub CI provides **source compilation and pure logic tests**, including Linux and FreeBSD NOSIX native compilation; it does not prove live VPN reachability.
- Kaminowaku's current CI performs `-fsyntax-only` builds and fixture tests, not full executable linking against freshly packaged native NOSIX 1.5 on both OSes.
- The project-wide `kscan` batch sender retains aggregate TX error events; it does not currently persist one ICMP failure record per target for `targets del -n`. The `-n` filter considers **explicit persisted per-target ICMP failures**; generic TCP/UDP or aggregate batch errors do not qualify.
- IPv6-over-TUN is not implemented in NOSIX BetaV3. The fallback failure bit is accepted by the generic per-family filter, but real IPv6-over-TUN TX remains unsupported.
- BPF tunnel device-specific output must be validated on the target FreeBSD image; BPF compilation and loopback smoke tests alone are insufficient.

## Manual merge gate

Only after native testing and evidence review: merge the compatible `NOSIX/BetaV3` and `kaminowaku/BetaV3` branches manually, following the repository's normal review and release process. Do **not** manually merge until the 1.5 ABI binaries/manifest are rebuilt for both platforms and the application has been tested with them.
