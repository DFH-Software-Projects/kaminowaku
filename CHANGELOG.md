# Changelog

All notable user-visible changes to Kaminowaku are recorded here.

Kaminowaku is currently pre-1.0. Entries describe released behavior rather than internal development checkpoints.

## 0.2.0 — Unreleased

### Changed

- new `beta-v2` terminal UI with continuous and frame display modes, top-anchored input, and differential screen updates.

### Fixed

- terminal viewport row alignment, stale prompt rendering, and ANSI/tab handling.
- ANSI color state carried across logical and wrapped output lines, including title borders.

## 0.1.3 — 2026-09-23

### Added

- hard monotonic receive deadlines for ICMP, DNS, TCP/UDP scanning, banner enrichment and Book execution;
- deadline regression tests and Linux/FreeBSD build checks.

### Changed

- bundled refreshed Linux and FreeBSD NOSIX ABI 1.4 builds from NOSIX source commit `f27b63ae1b3b26c15f87ca15c4c92c5d4e791e78`;
- native receive, route/neighbor lookup and Book sessions now observe bounded deadlines, including interrupted operations and partial I/O.

## 0.1.2 — 2026-09-23

### Added

- project-level `targets display -p` and `-b` filters now accept the scanner port grammar: single ports, inclusive ranges, comma-separated combinations, and multiple expressions;
- shared port-expression parsing keeps scanner and project-display selection semantics aligned.
- offline source-built installation with bundled OpenSSL 3.5.8 static libraries and a licensed private NOSIX ABI;
- opt-in online installation against system-managed OpenSSL 3 shared libraries; missing build prerequisites may be installed through apt or FreeBSD pkg only when explicitly requested;
- matching private NOSIX integrity, loader-path and OpenSSL linkage verification for both modes.

### Changed

- offline and online installation always discard stale build artifacts, compile the current source and install the freshly compiled Kaminowaku executable; checked-in Linux/FreeBSD prebuilt executables and their manifests have been removed;
- project port filters include a target when any selected TCP port is OPEN in the latest persisted IPv4 or IPv6 state; `-p` renders only selected TCP observations, while `-b` additionally renders detail and stored banner/service data for selected OPEN TCP ports; Book output remains collapsed;
- persisted port filtering now evaluates selected-port state in one pass per target and preserves newer CLOSED/FILTERED/ERROR results over older OPEN observations.
- offline remains the default and never invokes package managers or downloaders; online builds follow OS OpenSSL security updates without requiring new OpenSSL blobs in this repository;
- uninstallation preserves all user projects, captures and logs unless `--purge-user-data` is requested;
- removed temporary offline deployment smoke-test scripts from the release source tree.
- Makefile installation now defaults to a release build, supports online/offline OpenSSL and forwards custom install prefixes; `make uninstall` preserves user data unless `PURGE_USER_DATA=1` is explicitly supplied.

## 0.1.0 — 2026-09-21

Initial documented Kaminowaku release baseline.

### Added

- project-oriented and target-oriented scanning workflow;
- persistent target IDs, metadata, scan state, and evidence storage;
- IPv4 and IPv6 target support, including CIDR expansion;
- ICMPv4 and ICMPv6 discovery;
- DNS resolution through the configured Kaminowaku/NOSIX path;
- TCP and UDP port enumeration with single ports, ranges, lists, and full scans;
- TCP banner and service enrichment;
- profile-controlled network, DNS, capture, and resource-limit behavior;
- project, target, and external-tool contexts;
- target display and project-level result filtering;
- native TX/RX byte accounting in the TUI;
- NOSIX-backed native network runtime;
- Book execution through Kaminowaku's native C Lua-compatible runtime;
- managed Book TCP, UDP, TLS, capture, and result interfaces;
- system Books for SSH key-exchange enumeration and web enumeration;
- reusable SSH and HTTP protocol modules;
- per-Book PCAP evidence and atomic `BOOKNAME.out` persistence;
- failure/BAIL behavior that preserves previous successful Book output while retaining current PCAP evidence;
- registration and PTY execution of external tools from target context;
- per-target raw external-tool output artifacts;
- sanitizer-enabled debug builds and optimized release builds;
- Linux and FreeBSD build/install support;
- user-facing README, Book language reference, and source/control-flow link graph.

### Security and usage

- root privilege check at startup;
- bounded Book lexer/parser/runtime resources;
- restricted Book module namespaces and sandbox;
- authorized-use warning and project Acceptable Use Policy.
