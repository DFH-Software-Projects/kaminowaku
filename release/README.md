# Native release packaging

Kaminowaku supports two OpenSSL strategies. Offline is the default. Both
installation modes **always clean and compile the source in the deployed tree**.

## Offline mode: source-built, no downloads

```sh
./install.sh check --offline
sudo ./install.sh install BUILD=release --offline
```

An offline deployment includes Kaminowaku source, its Makefile, a local
Clang/Make toolchain on each target, the licensed platform-specific NOSIX ABI,
and pinned OpenSSL 3.5.8 source, generated headers and static libraries with
their integrity manifests under `libs/`. The installer validates the bundled
dependencies, discards stale `.STAGE/` outputs, compiles the current Kaminowaku
source and installs that exact newly built executable.

**No prepackaged Kaminowaku executables or executable manifests are shipped or
used by offline installation.** The `release/<platform>-amd64/bin/kaminowaku`
files from earlier snapshots have been removed. Bundled NOSIX and OpenSSL
libraries are still required to build without network access. Offline mode
never invokes package managers or downloaders; prepare Clang/Make beforehand.

To prepare refreshed offline OpenSSL dependencies natively on each platform:

```sh
./libs/openssl/build-native.sh
./install.sh all BUILD=release --offline
```

Collect the platform-specific OpenSSL headers, static archives and NOSIX ABI
into the source package. Do not include native Kaminowaku binaries: each
installation builds its own. Updating OpenSSL requires rebuilding its bundled
offline libraries.

## Online mode: OS-managed OpenSSL

```sh
sudo ./install.sh install BUILD=release --online
```

Online installation likewise cleans and recompiles Kaminowaku from the current
source, but links against system-managed OpenSSL 3 shared libraries. This mode
may install missing prerequisites via apt on supported Linux systems or pkg
on FreeBSD; `check` and `all` never install packages. NOSIX remains a
private, licensed shared library installed alongside Kaminowaku.

## Release verification

On Linux and FreeBSD, verify both modes and inspect the newly installed
executable, not just a cached checkout or an older copy elsewhere on PATH:

```sh
./install.sh check --offline
sudo ./install.sh install BUILD=release --offline
/usr/local/bin/kaminowaku
./install.sh check --online
sudo ./install.sh install BUILD=release --online
```

The offline binary must not dynamically link system `libssl.so` or
`libcrypto.so`. Both modes must load `libnosix.so.1` from Kaminowaku's
private library directory. If using a custom PREFIX, check `$PREFIX/bin`
instead of `/usr/local/bin`. Test TLS, Books and scanning on both OSes
before release.
