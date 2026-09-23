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

**Kaminowaku 0.1.0** is a low-level network enumeration framework for Linux and FreeBSD. It organizes work into projects and targets, performs ICMP, DNS, TCP, UDP, banner, HTTP/TLS, and Book-driven enumeration, and persists scan evidence alongside each target.

Kaminowaku uses **NOSIX** for network I/O and **OpenSSL** for TLS and cryptographic support. Books use Lua syntax but execute through Kaminowaku's own C runtime; an external Lua interpreter is not required.

> **Authorized use only.** Use Kaminowaku only against systems and networks you own or are explicitly authorized to test.

## Contents

- [Installation](#installation)
- [Uninstalling Kaminowaku](#uninstalling-kaminowaku)
- [Starting Kaminowaku](#starting-kaminowaku)
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

The current packaged 0.1.0 release supports:

- Linux x86-64 / amd64
- FreeBSD x86-64 / amd64

Additional architectures may be supported in future releases.

### Requirements

The installer requires:

- `clang`
- `make`
- the bundled static OpenSSL 3.5.8 archives for the target OS
- the bundled compatible NOSIX ABI

The installer invokes **no package manager** and performs **no dependency downloads**. A complete offline release also carries platform-specific prebuilt executables under `release/`; when present, installation requires neither `clang` nor `make`. For an explicitly requested source build (or a checkout without prebuilt binaries), the target must already have `clang` and `make`. Both OpenSSL archives and the NOSIX ABI must be staged before packaging a release. See [Offline native release binaries](release/README.md).

The release tree includes the packaged NOSIX ABI under `libs/nosix/`. The installer validates that bundled ABI for the detected platform and architecture, reconstructs the required shared-library links, and installs it with Kaminowaku.

Vendored OpenSSL is prepared on native Linux and FreeBSD build hosts before release. Instructions: [Bundled OpenSSL](libs/openssl/README.md). The installer does not build OpenSSL.

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
./install.sh check
```

The preflight check validates the source tree, runtime assets, vendored OpenSSL archives/checksums, and the NOSIX/OpenSSL ABI link path.

### Install a release build

The default installer first looks for a validated `release/<platform>-amd64/bin/kaminowaku` and its SHA-256 manifest. This path installs without a compiler or package manager. If the native binary is not present, it compiles Kaminowaku from the included source and prebuilt vendored libraries using existing `clang` and `make`.

#### Install a release build

```sh
sudo ./install.sh install BUILD=release
```

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
sudo ./install.sh install BUILD=debug
```

For normal use, prefer `BUILD=release`.

## Uninstalling Kaminowaku

The supplied uninstall script is **destructive**:

```sh
sudo ./uninstall.sh
```

It removes:

- `/usr/local/bin/kaminowaku`;
- `/usr/local/share/kaminowaku/`;
- Kaminowaku's private NOSIX shared libraries under `/usr/local/lib/kaminowaku/` (not global NOSIX or OpenSSL);
- `/root/.kaminowaku`;
- matching `.kaminowaku` datastores under `/home/*/` and `/usr/home/*/`.

That includes stored projects, target data, PCAP evidence, Book output, profiles, logs, and tool registrations. Back up any evidence or configuration you need before running the script.

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
| `debug on\|off` | Toggle debug rendering and telemetry |
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

### Remove unobserved targets

```text
targets del -n
```

This deletes targets with no received-packet observation while preserving targets backed by received network evidence. It is useful after broad CIDR discovery to remove systems that never responded.

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
| `targets display -p 443` | Only targets with TCP/443 marked open |
| `targets display -b 443` | Detailed built-in banner/service data for open TCP/443 |

Project-context aliases:

| Alias | Expansion |
| --- | --- |
| `d` | `targets display` |
| `dd` | `targets display -d` |

The `-p` and `-b` filters intentionally keep Book output collapsed.

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

### 6. Remove unobserved targets

```text
targets del -n
```

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
| `install` | Clean, build, install the binary, and install runtime assets |
| `all` | Clean and build without installing |
| `clean` | Remove build artifacts |
| `info` | Print build and installation configuration |

Examples:

```sh
./install.sh check
sudo ./install.sh install BUILD=release
./install.sh all BUILD=release
./install.sh clean
./install.sh info
```

The build uses a generated `.STAGE/` tree for header projection, object files, manifests, and the final binary. `.STAGE` is build state, not user data.

## Developer documentation

- [Source link graph](docs/LINK_GRAPH.md) — source ownership, compile/link relationships, runtime flow, external dependency boundaries, and change-impact mapping.
- [Book language reference](docs/BOOK_LANGUAGE_V1.md) — supported Lua syntax, Book/module authoring contract, runtime boundaries, and guidance for Books versus external tools/scripts.

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
