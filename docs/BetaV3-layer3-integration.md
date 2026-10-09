# BetaV3 · Phase 4: NOSIX Layer-3 Integration

Scope: Kaminowaku/BetaV3. NOSIX/BetaV3 must be rebuilt and packaged separately before executable integration tests. Phase 5 target-pruning changes are not included here.

## NOSIX ABI contract

- Requires the additive NOSIX ABI **1.5** public header and native runtime **libnosix.so.1.5.0** for each platform.
- `NOSIX_ERR_L3_FALLBACK_FAILED (-11)` denotes an **attempted failed L3 send**, not a missing route, neighbor, or response timeout.
- `NOSIX_TX_SURFACE_IPV4_L3` denotes successful IPv4 TX without Ethernet framing.
- `NOSIX_CAPTURE_IPV4_L3` denotes RX buffers starting at the IPv4 header.
- The checked-in `libs/nosix/linux|freebsd/lib` binaries and abi.env metadata remain **1.4** until the deployment script replaces them with native 1.5 builds from NOSIX/BetaV3. We deliberately do **not** rename old binaries or pretend they implement the new ABI.

## Application paths

1. `kwire.h` maps Ethernet TX/RX to PCAP DLT 1, and raw IPv4 (host-local or tunnel) to DLT 101. It increments TX/RX counters only after actual writes or matched received packets, as before.
2. When manual TX and RX interfaces deliver different packet linktypes, matched RX evidence is kept in a sibling `*-rx-ipv4.pcap` or `*-rx-ethernet.pcap`. Classic PCAP supports one network/linktype per file, so neither header is fabricated and no received evidence is dropped silently.
3. ICMPv4, TCP/UDP port scans and DNS UDP response matching accept either NOSIX raw-IPv4 capture flag. Their checks for source/destination, checksums, ports and transaction IDs remain intact.
4. ICMPv4 maps `NOSIX_ERR_L3_FALLBACK_FAILED` to `SCAN_RESULT_RAN | SCAN_RESULT_ERROR | SCAN_RESULT_L3_FALLBACK`; the target display reports `L3_FALLBACK`. Existing stored scan data shape/version remains unchanged; bit 6 was free.
5. The IPv4 project scan scheduler can lock auto-selected egress after a successful Ethernet **or** IPv4 L3 TX. TCP/UDP port scans explicitly report the dedicated fallback error, while generic per-port persisted evidence remains `NETWORK_ERROR`; Phase 5 will address separate target-pruning evidence and safe deletion semantics.
6. Managed IPv6-over-TUN is unsupported in NOSIX Phase 3; the symbolic status is available for logging, but no new IPv6 L3 write behavior is claimed.

## Build/verification

Source-only Linux CI checks all Kaminowaku C translation units against the vendored NOSIX **1.5 header** and runs the `tests/betav3_l3_contract.c` fixture for DLT mapping and mixed-link PCAP output. This is not an executable scanner validation: the currently checked-in shared libraries are still **1.4**.

The release/deployment workflow must rebuild NOSIX/BetaV3 natively on **Linux and FreeBSD**, copy each **1.5.0** runtime binary and `abi.env` alongside the 1.5 header, then build/install Kaminowaku/BetaV3. The Makefile/install.sh reject the mismatched 1.4 package.

Operator validation requires an active TUN VPN and an echo-responding permitted test target:

```sh
ip -4 route get 192.168.123.104    # Linux example
# Configure Kami with tx.interface=tun0, rx.interface=same, gateway auto
# Run ping -4, TCP/UDP port scans, and DNS over the tunnel.
# Check target ICMP status, counters, outgoing/incoming bytes and PCAP linktype.
```

For FreeBSD, repeat on a supported BPF tunnel interface with the native FreeBSD route and capture tools. Phase 5 extends `targets del -n` with the explicitly recorded L3 failure bit while preserving its fail-closed deletion policy.
