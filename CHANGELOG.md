# Changelog

All notable user-visible changes to Kaminowaku are recorded here.

Kaminowaku is currently pre-1.0. Entries describe released behavior rather than internal development checkpoints.

## Unreleased

### Added

- offline installation with bundled OpenSSL 3.5.8 static libraries, verified native Linux/FreeBSD amd64 executables, and licensed private NOSIX ABI;
- opt-in online installation against system-managed OpenSSL 3 shared libraries; missing build prerequisites may be installed through apt or FreeBSD pkg only when explicitly requested;
- matching private NOSIX integrity, loader-path and OpenSSL linkage verification for both modes.

### Changed

- offline remains the default and never invokes package managers or downloaders; online builds follow OS OpenSSL security updates without requiring new OpenSSL blobs in this repository;
- uninstallation preserves all user projects, captures and logs unless `--purge-user-data` is requested;
- removed temporary offline deployment smoke-test scripts from the release source tree.

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
