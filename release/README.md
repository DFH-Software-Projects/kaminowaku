# Native release packaging

Kaminowaku supports two OpenSSL strategies. Choose the mode explicitly when
installing; an unspecified mode defaults to **offline**.

## Offline mode: reproducible, no downloads

```sh
sudo ./install.sh install --offline
```

A complete offline release includes these prebuilt native executables:

- `release/linux-amd64/bin/kaminowaku` with its `BUILD-MANIFEST.txt`;
- `release/freebsd-amd64/bin/kaminowaku` with its `BUILD-MANIFEST.txt`.

The offline package also carries the licensed NOSIX ABI, pinned OpenSSL 3.5.8
source tarball, per-platform generated headers, `libssl.a`, `libcrypto.a`,
and integrity manifests under `libs/`. The installer verifies SHA-256
manifests before using the native executable, installs NOSIX to the private
`PREFIX/lib/kaminowaku/` directory, and does not invoke a compiler,
package manager or network downloader on the prebuilt path.

Native release **preparation** on each build host is a separate operation:

```sh
./libs/openssl/build-native.sh
./install.sh all BUILD=release --offline
./release/package-native.sh
```

The native OpenSSL preparation script requires its build-time prerequisites
on the release host, including Perl. The package-native script refuses to
package an online/system-OpenSSL build as an offline release. Copy the Linux
and FreeBSD native outputs into the same Git release checkout.

The offline snapshot uses the OpenSSL version packaged at release time;
new OpenSSL security updates require new offline libraries and binaries.
This offline source archive is **not** required by online installation.

## Online mode: OS-managed OpenSSL

```sh
sudo ./install.sh install --online
```

Online mode ignores all bundled OpenSSL source, headers, libraries and
prebuilt static executables. It requires a local Clang/Make toolchain and
OpenSSL 3 development metadata. If missing, this explicitly opted-in install
mode may acquire prerequisites with `apt-get` on apt-based Linux (including
Kali) or `pkg` on FreeBSD. Already-satisfied dependencies trigger no
package-manager operations. A `check --online` or `all --online` invocation
is read-only with respect to package installation.

Kaminowaku is rebuilt against the installed **shared** OpenSSL libraries;
OpenSSL upgrades within the same ABI are handled by the OS package manager.
After an incompatible ABI/SONAME change, rebuild/reinstall Kaminowaku.
NOSIX is still installed as a private, licensed shared library.

`./release/package-native.sh` is deliberately **offline only**; do not
publish a system-dependent online build in place of the verified offline
native executable.

## Release verification before merging

On both supported operating systems, validate the actual production build
and installation paths:

```sh
./install.sh check --offline
./install.sh check --online
./install.sh all BUILD=release --online
sudo ./install.sh install --online
sudo ./install.sh install --offline
```

Verify `ldd` output: online must link system `libssl.so` and
`libcrypto.so`, offline must **not**, and both must resolve
`libnosix.so.1` from the private Kaminowaku library directory. Check TLS
certificate validation, Books and the scanning workflow before the release.

Compatibility is limited by each native binary's libc and OS baseline.
Build Linux on the oldest supported libc and FreeBSD on the oldest intended
supported OS release; test those baselines before making wider claims.

Do not ship temporary deployment smoke-test scripts in the main repository.
