```text
+-----------------------------------------+
|⠄⣾⣿⡇⢸⣿⣿⣿⠄⠈⣿⣿⣿⣿⠈⣿⡇⢹⣿⣿⣿⡇⡇⢸⣿⣿⡇⣿⣿⣿|
|⢠⣿⣿⡇⢸⣿⣿⣿⡇⠄⢹⣿⣿⣿⡀⣿⣧⢸⣿⣿⣿⠁⡇⢸⣿⣿⠁⣿⣿⣿|
|⢸⣿⣿⡇⠸⣿⣿⣿⣿⡄⠈⢿⣿⣿⡇⢸⣿⡀⣿⣿⡿⠸⡇⣸⣿⣿⠄⣿⣿⣿|
|⢸⣿⡿⠷⠄⠿⠿⠿⠟⠓⠰⠘⠿⣿⣿⡈⣿⡇⢹⡟⠰⠦⠁⠈⠉⠋⠄⠻⢿⣿|
|⢨⡑⠶⡏⠛⠐⠋⠓⠲⠶⣭⣤⣴⣦⣭⣥⣮⣾⣬⣴⡮⠝⠒⠂⠂⠘⠉⠿⠖⣬|
|⠈⠉⠄⡀⠄⣀⣀⣀⣀⠈⢛⣿⣿⣿⣿⣿⣿⣿⣿⣟⠁⣀⣤⣤⣠⡀⠄⡀⠈⠁|
|⠄⠠⣾⡀⣾⣿⣧⣼⣿⡿⢠⣿⣿⣿⣿⣿⣿⣿⣿⣧⣼⣿⣧⣼⣿⣿⢀⣿⡇⠄|
|⡀⠄⠻⣷⡘⢿⣿⣿⡿⢣⣾⣿⣿⣿⣿⣿⣿⣿⣿⣿⣜⢿⣿⣿⡿⢃⣾⠟⢁⠈|
|⢃⢻⣶⣬⣿⣶⣬⣥⣶⣿⣿⣿⣿⣿⣿⢿⣿⣿⣿⣿⣿⣷⣶⣶⣾⣿⣷⣾⣾⢣|
+-----------------------------------------+
```

Banner source: [Emoji Combos](https://emojicombos.com/)

# Kaminowaku

**Kaminowaku 0.1.3** is a low-level network enumeration framework for Linux and FreeBSD. It organizes work into projects and targets, performs ICMP, DNS, TCP, UDP, banner, HTTP/TLS, and Book-driven enumeration, and persists scan evidence alongside each target.

Kaminowaku uses **NOSIX** for network I/O and **OpenSSL** for TLS and cryptographic support. Books use Lua syntax but execute through Kaminowaku's own C runtime; an external Lua interpreter is not required.

> **Authorized use only.** Use Kaminowaku only against systems and networks you own or are explicitly authorized to test.

## Contents

- [Installation](#installation)
- [Uninstalling Kaminowaku](#uninstalling-kaminowaku)
- [Starting Kaminowaku](#starting-kaminowaku)
- [Terminal UI and display modes](#terminal-ui-and-display-modes)
- [Workflow](#workflow)
- [Command reference](#command-reference)
- [Projects](#projects)
- [Targets](#targets)
- [Target context](#target-context)
- [Profiles](#profiles)
- [Ping](#ping)
- [DNS resolution](#dns-resolution)
- [Port scanning](#port-scanning)
- [Books](#books)
- [External tools](#external-tools)
- [Display and filtering](#display-and-filtering)
- [Data and evidence](#data-and-evidence)
- [Typical workflow](#typical-workflow)
- [Aliases](#aliases)
- [Build and installer reference](#build-and-installer-reference)
- [Developer documentation](#developer-documentation)
- [Contributing](#contributing)
- [Security](#security)
- [Changelog](#changelog)
- [License](#license)
- [Support](#support)
- [Development acknowledgment](#development-acknowledgment)

## Installation

### Supported platforms

The current packaged 0.1.3 release supports:

- Linux x86-64 / amd64
- FreeBSD x86-64 / amd64

Additional architectures may be supported in future releases.

### Requirements

The installer offers **two explicit OpenSSL modes**. `--offline` is the default and uses the pinned, statically linked OpenSSL 3.5.8 native release. It requires a locally available Clang/Make toolchain but never invokes a package manager or downloader; the installer always compiles the source being installed. `--online` builds Kaminowaku against the operating system's **OpenSSL 3 shared libraries** and permits dependency installation through `apt` on Kali/Debian or `pkg` on FreeBSD *only if dependencies are missing*. Online installation requires Clang, Make, OpenSSL development headers and pkg-config/pkgconf; the installer can acquire them with your explicit `--online` choice.

The online mode does **not** require the bundled OpenSSL source archive, static libraries or manifests. Security updates for a dynamically linked OpenSSL can come through your operating system; an incompatible OpenSSL ABI upgrade may still require rebuilding Kaminowaku. The pinned offline build remains a snapshot and must be separately refreshed to include future OpenSSL security fixes.

Both modes use the same licensed, bundled NOSIX ABI under `libs/nosix/`, installed into Kaminowaku's **private** library directory. Neither mode overwrites a system OpenSSL installation. See [Release packaging](release/README.md) and [Bundled OpenSSL](libs/openssl/README.md).

Required NOSIX components:

| Component | Purpose |
| --- | --- |
| `nosix.h` | Core NOSIX interface |
| `nosix_poll.h` | Poll/capture interface |
| `nosix_datagram.h` | Datagram interface |
| `libnosix.so` | Shared NOSIX library |

### Clone

```sh
git clone https://github.com/DFH-Software-Projects/kaminowaku.git
cd kaminowaku
```

### Preflight check

```sh
./install.sh check --offline
./install.sh check --online
```

Both checks are non-installing: they may reconstruct linker symlinks within the shipped NOSIX ABI but never modify system packages. Offline mode validates bundled libraries and the required local Clang/Make toolchain; online mode checks system OpenSSL 3 development dependencies, Clang, Make, and pkg-config/pkgconf. Unlike `install --online`, `check --online` never downloads packages.

### Offline installation (default)

```sh
sudo ./install.sh install --offline
```

Offline installation **always performs a clean source build**, links the included NOSIX ABI and verified native OpenSSL static libraries, then installs the binary just compiled. A local Clang/Make toolchain is required. Packaged Kaminowaku executables are not shipped or used. **Offline mode never pulls packages.**

### Online installation (system OpenSSL)

```sh
sudo ./install.sh install --online
```

Online mode always compiles Kaminowaku against system-managed OpenSSL 3 shared libraries rather than reusing the statically linked offline binary. When dependencies are missing, this explicit install mode may run `apt-get` or FreeBSD `pkg`; when they're already present, it makes no package-manager calls. On a restricted host, use `--offline` instead.

For an online build without installing:

```sh
./install.sh all BUILD=release --online
```

Build-only commands do not install packages; prepare any missing dependencies in advance.

### Makefile install and uninstall

The Makefile delegates installation and removal to the same validated scripts.
A bare `make` remains a sanitizer-enabled debug source build, whereas
`make install` defaults to a clean **release/offline source build** and installation.

```sh
sudo make install                              # release, offline; no package pulls
sudo make install OPENSSL_MODE=online          # release, system-managed OpenSSL
sudo make install INSTALL_BUILD=debug         # debug, offline source build
sudo make uninstall                           # preserve user projects, PCAPs and logs
sudo make uninstall PURGE_USER_DATA=1         # explicitly remove ALL users' Kami data
```

To install into or remove from a non-default prefix, pass `PREFIX=/your/path`
to the respective Make target. `make install` uses `INSTALL_BUILD` rather
than `BUILD` so the ordinary development build retains its debug default.
An explicit `PURGE_USER_DATA=1` is required before the Makefile passes
`--purge-user-data` to the uninstaller.

Installed binary:

```text
/usr/local/bin/kaminowaku
```

Installed runtime assets:

```text
/usr/local/share/kaminowaku/
├── books/
│   ├── main/
│   └── modules/
├── profiles/
└── tools/
```

System default profile:

```text
/usr/local/share/kaminowaku/profiles/default.ini
```

### Install a debug build

The installer defaults to a release build when `BUILD` is omitted. Use `BUILD=debug` explicitly when you want the sanitizer-enabled development build.

```sh
sudo ./install.sh install BUILD=debug --offline
```

For normal use, prefer `BUILD=release`. For a sanitizer-enabled system-OpenSSL build, specify `--online` instead of `--offline`.

## Uninstalling Kaminowaku

By default, the uninstaller removes the executable, shared runtime assets, and private NOSIX library **without deleting user evidence or configuration**:

```sh
sudo ./uninstall.sh
```

The default preserves `/root/.kaminowaku` and matching datastores under `/home/*/` and `/usr/home/*/`. To additionally erase **all users'** projects, target data, PCAP evidence, Book outputs, profiles, logs and tool registrations, explicitly request:

```sh
sudo ./uninstall.sh --purge-user-data
```

Back up evidence before using the purge option. Neither mode removes system OpenSSL or an independently installed NOSIX library.

## Starting Kaminowaku

Kaminowaku requires root privileges because its network operations use low-level packet and capture facilities.

```sh
sudo kaminowaku
```

At startup, Kaminowaku initializes its datastore, profile, runtime log, target index, and TUI. Press Enter when prompted to continue.

Runtime datastore:

```text
~/.kaminowaku/
├── books/
│   └── modules/
├── logs/
├── profiles/
├── projects/
└── tools/
```

Because Kaminowaku currently requires root, `~/.kaminowaku` normally refers to the root user's home directory when launched with `sudo`.

The TUI expects a terminal at least 24 rows tall.

## Terminal UI and display modes

The `beta-v2` TUI has two operator-selectable display modes. **Continuous** is the default; it preserves scrollback across commands. **Frame** presents the current command frame rather than continuously accumulating the entire prior view. The `ui` command works from project, target and tool contexts:

```text
ui
ui mode continuous
ui mode frame
```

The terminal input decoder supports fragmented escape sequences, navigation/editing keys, mouse-wheel scrolling and bracketed paste. KUI owns prompt painting and screen updates through an ordered event queue and differential screen renderer. Interactive external tools use a dedicated PTY handoff so their terminal state is restored when control returns to Kaminowaku. `debug on/off` controls debugging; it is not the display-mode switch.

For implementation details see the [TUI architecture and control flow](docs/LINK_GRAPH.md#ui-and-rendering-path). To exercise the permanent C regression suite on Linux or FreeBSD:

```sh
sh tests/run-tui-regressions.sh
sh tests/run-tui-regressions.sh --bench
```

See [TUI regression coverage and manual acceptance](tests/README.md); real-terminal acceptance testing remains necessary.

## Workflow

Kaminowaku is context-oriented. Operations become more specific as you move from a project into a target and, optionally, into an external-tool session.

| Context | Scope | Typical operations |
| --- | --- | --- |
| **Project** | Entire active project | Add targets, resolve, ping, scan, run Books, filter results |
| **Target** | One active TID | Inspect, modify, scan, run Books, enter tool context |
| **Tool** | One active target | Execute registered external tools inside the target directory |

Typical flow:

```text
┌───────────────────────┐
│ Create / load project │
└───────────┬───────────┘
            │
            v
┌───────────────────────┐
│ Add / resolve targets │
└───────────┬───────────┘
            │
            v
┌───────────────────────┐
│ Discover live systems │
│     ping -4 / -6      │
└───────────┬───────────┘
            │
            v
┌───────────────────────┐
│ Enumerate ports       │
│ scan -t / -u / -F     │
└───────────┬───────────┘
            │
            v
┌───────────────────────┐
│ Filter / inspect data │
│ d / dd / -o / -p / -b │
└───────────┬───────────┘
            │
            v
┌───────────────────────┐
│ Enter target context  │
└───────────┬───────────┘
            │
            v
┌───────────────────────┐
│ Books / tools / detail│
└───────────────────────┘
```

Use `help` at any time to see commands valid for the current context. Use `help <command>` for command-specific usage.

## Command reference

| Command | Purpose |
| --- | --- |
| `help [command]` | Show the command list or command-specific help |
| `pwd` | Show Kaminowaku's current datastore directory |
| `interfaces` | List available network interfaces |
| `profile` | Display, load, modify, or save the global profile |
| `new [project]` | Create a project |
| `load [project]` | Load an existing project |
| `unload` | Unload the active project |
| `targets ...` | Add, delete, display, or enter targets |
| `ping -4\|-6` | Perform ICMP discovery |
| `resolve` | Resolve target URLs through DNS |
| `scan ...` | Run TCP/UDP enumeration |
| `book [name]` | List or execute Books |
| `tool ...` | Register/manage tools or enter tool context |
| `ui` / `ui mode continuous\|frame` | Inspect or change terminal output mode (all contexts) |
| `debug on\|off` | Toggle debug output |
| `exit` | Exit Kaminowaku |

## Projects

Projects contain targets and their persisted scan and evidence data.

### Create a project

```text
new assessment
```

Running `new` without a name enters a project-name prompt:

```text
new
```

Press Enter without entering a name to cancel.

If another project is already loaded, Kaminowaku unloads it before creating the new project.

### Load a project

```text
load assessment
```

Running `load` without a name lists available projects and prompts for one.

### Unload a project

```text
unload
```

Project data is persisted automatically during normal operation. Avoid forcefully terminating Kaminowaku while writes are in progress.

## Targets

Target commands require an active project.

Each target receives a Kaminowaku target ID (**TID**) and can contain:

- URL
- IPv4 address
- IPv6 address
- MAC address
- note
- persisted discovery and scan state
- Book output
- PCAP evidence
- external-tool output

### Add one target

| Identifier | Command |
| --- | --- |
| URL | `targets add -U example.com` |
| IPv4 | `targets add -4 192.0.2.10` |
| IPv6 | `targets add -6 2001:db8::10` |
| MAC | `targets add -M 00:11:22:33:44:55` |

### Add a CIDR network

IPv4:

```text
targets add -net -4 192.0.2.0/24
```

IPv6:

```text
targets add -net -6 2001:db8::/126
```

CIDR input is expanded into individual Kaminowaku targets.

### Delete a target

By TID:

```text
targets del <TID>
```

By identifier:

```text
targets del -U example.com
targets del -4 192.0.2.10
targets del -6 2001:db8::10
targets del -M 00:11:22:33:44:55
```

`delete` is also accepted in place of `del`.

### Remove targets with confirmed neighbor-resolution failures

```text
targets del -n
```

This deletes **only** targets with an explicitly recorded NOSIX neighbor-resolution failure (`NOSIX_ERR_NEIGHBOR`) for **every configured IP address family**, provided there is no recorded ICMP reply or positive port-scan receive evidence. The failure means NOSIX could not resolve the link-layer address of the required **next hop**; it does **not** mean the target simply failed to reply to ICMP.

An ICMP timeout, a generic transmit error, a route failure, an unscanned target, or missing/older scan metadata is **not** sufficient for deletion. For a dual-stack target, both IPv4 and IPv6 must have explicit neighbor-resolution failures; an untested or inconclusive family preserves the entire target. Targets whose stored scan or port data contains received-packet evidence are also preserved. If target data cannot be read, Kaminowaku preserves it rather than deleting blindly.

**Routed-network caution:** neighbor resolution may be for a gateway rather than the destination host. A failure to resolve that gateway does not establish that the remote target is offline. Use `targets del -n` only when its next-hop failure semantics match the cleanup you intend; `targets display -o` remains the non-destructive way to show targets backed by received-packet observations.

## Target context

Enter a target by TID:

```text
targets <TID>
```

Or locate it by identifier:

```text
targets -U example.com
targets -4 192.0.2.10
targets -6 2001:db8::10
targets -M 00:11:22:33:44:55
```

The prompt changes to the active TID. Inside target context, `ping`, `resolve`, `scan`, `book`, `display`, and `tool` operate on only the active target.

Return to project context:

```text
back
```

### Modify target metadata

```text
set -U example.com
set -4 192.0.2.10
set -6 2001:db8::10
set -M 00:11:22:33:44:55
```

Notes must be quoted, and `-N` must be the final parameter:

```text
set -N "Primary web server"
```

Multiple fields can be changed in one command:

```text
set -4 192.0.2.10 -N "Primary web server"
```

If validation fails, Kaminowaku rejects the update rather than partially persisting invalid target state.

## Profiles

The global profile controls transmit, receive, DNS, and resource-limit behavior.

### Display the active profile

```text
profile
profile display
```

### Load a profile

```text
profile load default
```

Running `profile load` without a profile name lists available profiles and prompts for one.

### Change a runtime profile value

Use `section.key` form:

```text
profile set tx.interface eth0
profile set rx.interface same
profile set rx.timeout_ms 2500
profile set dns.server1 1.1.1.1
profile set limits.max_tx_rate_pps 500
```

Bare keys are accepted only when unique. For ambiguous keys such as `interface`, `strict`, and `timeout_ms`, use the full `section.key`.

Fields that permit an empty value can be cleared with either:

```text
profile set tx.gateway empty
profile set tx.gateway ""
```

### Save the active runtime profile

Save as a new profile:

```text
profile save lab
```

Explicitly replace an existing profile:

```text
profile save lab overwrite
```

### Profile sections

| Section | Controls |
| --- | --- |
| `profile` | `name`, `version`, `scope` |
| `tx` | Interface, source address, gateway, TTL/hop limit, ID/source-port generation, checksum policy |
| `rx` | Interface, timeout, promiscuous mode, snap length, receive state, deduplication, match policy |
| `dns` | Primary/secondary DNS servers, search domain, timeout, retries |
| `limits` | Pending transactions, capture ceilings, TX/RX rates, DNS concurrency, target batches, scan workers |

Useful shipped defaults include:

| Setting | Default |
| --- | ---: |
| RX timeout | 3000 ms |
| RX snap length | 65535 bytes |
| RX match policy | `strict` |
| Maximum TX rate | 1000 packets/s |
| Scan workers | 8 |

## Ping

Kaminowaku supports ICMPv4 and ICMPv6 discovery.

Project context:

```text
ping -4
ping -6
```

The operation runs against every target in the active project.

Target context:

```text
ping -4
ping -6
```

Only the active target is probed.

Kaminowaku persists discovery state and distinguishes received replies, timeouts, and relevant network-path errors.

## DNS resolution

```text
resolve
```

In project context, Kaminowaku resolves applicable target URLs across the project. In target context, it resolves only the active target.

DNS traffic is generated through Kaminowaku/NOSIX using the configured `[dns]` profile servers. Kaminowaku does not silently fall back to the operating system resolver.

## Port scanning

Port specifications accept single ports, ranges, comma-separated lists, and mixed combinations.

### TCP

```text
scan -t 443
scan -t 22,80,443
scan -t 1-1024
scan -t 22,80,443,8000-8100
```

### UDP

```text
scan -u 53
scan -u 53,123,161
scan -u 1-1024
```

### TCP and UDP together

```text
scan -t 22,80,443 -u 53,123
```

### Full scan

```text
scan -F
```

`-F` scans TCP and UDP ports 1 through 65535. This is a large operation and should be used intentionally.

Project context scans every target. Target context scans only the active target.

Open TCP results are automatically enriched with passive banner handling and safe HTTP/TLS probing where applicable. Persisted built-in scan data can later be inspected with `display`, `targets display -d`, or port-specific filters.

## Books

Books are Kaminowaku's scriptable enumeration layer.

List available system and user Books:

```text
book
```

Run a Book:

```text
book ssh-kex
book web-enum
```

Current system Books:

| Book | Purpose |
| --- | --- |
| `ssh-kex` | SSH transport and key-exchange enumeration |
| `web-enum` | HTTPS/HTTP response enumeration |

At project level, a Book runs sequentially against every target. In target context, it runs only against the active target.

Each Book invocation uses a capture-gated Book session:

- a timestamped PCAP is retained for every executed target;
- successful Book output atomically replaces `BOOKNAME.out`;
- a failed or explicitly bailed Book preserves the previous `.out` snapshot;
- Book output is displayed after built-in scan data when full display is requested.

Book locations:

| Type | Path |
| --- | --- |
| System Books | `/usr/local/share/kaminowaku/books/` |
| User Books | `~/.kaminowaku/books/` |
| User modules | `~/.kaminowaku/books/modules/` |

Book names may contain letters, numbers, hyphens, and underscores.

For the supported Lua language, module model, runtime restrictions, and Book authoring interface, see [BOOK_LANGUAGE_V1.md](docs/BOOK_LANGUAGE_V1.md).

## External tools

Kaminowaku can register existing executables and launch them from a target-specific tool context.

### Register a tool

The path must be absolute and executable:

```text
tool add /usr/bin/nmap
```

### List registered tools

```text
tool list
```

### Remove a registration

```text
tool del nmap
```

Removing a registration does **not** delete the underlying executable.

### Enter tool context

First enter a target, then run:

```text
tool
```

The prompt changes to:

```text
[tools]>
```

Registered tools can then be executed directly with their normal arguments:

```text
nmap -sV 192.0.2.10
```

Kaminowaku launches the executable directly rather than through a shell. The active target directory becomes the working directory.

Interactive tools run through a PTY. Raw output is saved in the active target directory as:

```text
<tool>-<epoch_ns>.out
```

External-tool execution is intentionally separate from Kaminowaku-native network accounting:

- no Kaminowaku PCAP session is created;
- Kaminowaku TX/RX counters are not updated.

Return to target context:

```text
back
```

## Display and filtering

### Project display

| Command | Result |
| --- | --- |
| `targets display` | Basic project target and scan summary |
| `targets display -d` | Full persisted built-in scan detail, followed by stored Book output |
| `targets display -o` | Only targets/results backed by received-packet observations |
| `targets display -p 443` | Only targets with TCP/443 marked open; show selected TCP observations |
| `targets display -p 22,80,443,8000-8100` | Targets with any selected TCP port open; show only selected TCP observations |
| `targets display -p 20-25 80 443` | Multiple port expressions are unioned into one filter |
| `targets display -b 443` | Detailed built-in banner/service data for open TCP/443 |
| `targets display -b 20-25,80,443` | Targets with any selected TCP port open; add detail/banner data for selected OPEN TCP ports |

Project-context aliases:

| Alias | Expansion |
| --- | --- |
| `d` | `targets display` |
| `dd` | `targets display -d` |

The `-p` and `-b` filters use the same port-expression grammar as `scan`: single ports, inclusive ranges, comma-separated mixed lists, and multiple expressions. A target is included when any selected TCP port is currently OPEN in its latest persisted IPv4 or IPv6 result. Both filters keep Book output collapsed; `-p` renders selected TCP observations, while `-b` additionally renders built-in detail and stored banner/service data for selected OPEN TCP ports.

### Target display

Inside target context:

```text
display
```

Equivalent target-context aliases:

```text
dt
d
dd
```

Target display always renders the active target's built-in scan state first, followed by every stored Book `.out` snapshot for that target.

There is no per-Book display selector in target context.

## Data and evidence

Runtime datastore:

```text
~/.kaminowaku/
```

Project storage:

```text
~/.kaminowaku/projects/<project>/
```

Target storage:

```text
~/.kaminowaku/projects/<project>/<TID>/
├── .data
├── *.out
└── *.pcap
```

The target directory is the collection point for persisted target state, scan artifacts, Book output, PCAP evidence, and external-tool output.

Runtime logs:

```text
~/.kaminowaku/logs/
```

The top banner tracks runtime TX/RX byte accounting for Kaminowaku-native network activity.

## Typical workflow

The following example shows a normal authorized assessment flow.

### 1. Start Kaminowaku

```sh
sudo kaminowaku
```

### 2. Create a project

```text
new lab
```

### 3. Add targets

```text
targets add -U example.com
targets add -net -4 192.0.2.0/29
```

### 4. Resolve hostnames

```text
resolve
```

### 5. Perform discovery

```text
ping -4
```

### 6. Optionally prune confirmed neighbor failures

```text
targets del -n
```

Only targets with explicit next-hop neighbor-resolution failures across all configured IP families and no recorded reply evidence are deleted. Silent ICMP timeouts and unscanned targets remain; review the [neighbor-failure deletion rules](#remove-targets-with-confirmed-neighbor-resolution-failures) before using this on routed networks.

### 7. Scan common services

```text
scan -t 22,80,443 -u 53
```

### 8. Review observations

```text
targets display -o
targets display -p 443
```

### 9. Enter a target

```text
targets -4 192.0.2.10
```

### 10. Inspect target state

```text
display
```

### 11. Run a Book

```text
book web-enum
```

### 12. Review updated target data

```text
display
```

### 13. Return, unload, and exit

```text
back
unload
exit
```

## Aliases

| Alias | Expansion |
| --- | --- |
| `h` | `help` |
| `n` | `new` |
| `l` | `load` |
| `u` | `unload` |
| `d` | `targets display` in project context; `display` in target context |
| `dd` | `targets display -d` in project context; `display` in target context |
| `a` | `targets add` |
| `t` | `targets` |
| `p` | `profile` |
| `s` | `set` |
| `b` | `book` |
| `i` | `interfaces` |
| `r` | `resolve` |

## Build and installer reference

Installer syntax:

```text
./install.sh [target] [BUILD=debug|release]
```

| Target | Behavior |
| --- | --- |
| `check` | Validate compiler, dependencies, NOSIX ABI, and runtime assets |
| `install` | Clean-build the current source using the selected OpenSSL mode, then install the freshly compiled executable, private NOSIX and runtime assets |
| `all` | Clean and build without installing |
| `clean` | Remove build artifacts |
| `info` | Print build and installation configuration |

Examples:

```sh
./install.sh check --offline
./install.sh check --online
sudo ./install.sh install BUILD=release --offline
sudo ./install.sh install BUILD=release --online
./install.sh all BUILD=release --offline
./install.sh all BUILD=release --online
./install.sh clean
./install.sh info
```

The build uses a generated `.STAGE/` tree for header projection, object files, manifests, and the final binary. `.STAGE` is build state, not user data.

## Developer documentation

- [Source link graph](docs/LINK_GRAPH.md) — source ownership, beta-v2 TUI event/renderer control flow, compile/link relationships, external dependency boundaries, and change-impact mapping.
- [TUI regression suite](tests/README.md) — permanent input, event, compositor, wrap-index, shutdown and PTY tests, benchmark runner and interactive acceptance checklist.
- [Book language reference](docs/BOOK_LANGUAGE_V1.md) — supported Lua syntax, Book/module authoring contract, runtime boundaries, and guidance for Books versus external tools/scripts.
- [Hard timeout guarantees](docs/HARD_TIMEOUTS.md) — monotonic receive deadlines, Book execution limits, regression checks, and native NOSIX rebuild requirements.

## Contributing

Community contributions are currently focused on Lua Books and reusable Lua protocol modules. Contributors develop and publish the work in their **own repository**, link back to Kaminowaku, and submit that repository for maintainer review. The core C implementation remains maintainer-controlled.

See [CONTRIBUTING.md](CONTRIBUTING.md) for repository, testing, licensing, and submission expectations.

## Security

Do not publish an unpatched Kaminowaku vulnerability in a public issue. See [SECURITY.md](SECURITY.md) for supported versions, reporting guidance, scope, and coordinated-disclosure expectations.

## Changelog

Release-level changes are tracked in [CHANGELOG.md](CHANGELOG.md).

## License

Kaminowaku is licensed under the [BSD 3-Clause License](LICENSE.txt). Responsible-use expectations are documented separately in the [Acceptable Use Policy](AUP.md).

## Support

If Kaminowaku is useful to you and you want to support its continued development, you can [buy me a coffee](https://buymeacoffee.com/jamisonadrn).

## Development acknowledgment

I could not have finished Kaminowaku in the timeframe that I did without the use of AI-assisted development.

AI was used as a development accelerator throughout portions of the project, but all AI-generated code included in Kaminowaku was personally reviewed, tested, and checked by me before I accepted it into the codebase. Responsibility for the final implementation remains mine.
