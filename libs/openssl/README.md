# Vendored OpenSSL 3.5.8

Pinned source: https://github.com/openssl/openssl/releases/tag/openssl-3.5.8

Expected SHA-256 for `libs/openssl/source/openssl-3.5.8.tar.gz`:
`a8f84a39918ec6415ce765d9b429d313ba97b8143169c172e734b9514464f5b2`

## Native preparation (on developer/release machines only)

Verify the official upstream archive, then place it at the path above.
Run `./libs/openssl/build-native.sh` natively on Linux amd64 and again
on FreeBSD amd64, using existing clang, make, Perl and the upstream-required
Perl modules. This script validates the tarball SHA-256, runs OpenSSL's
native build/test procedure, builds static libraries with
`no-shared no-module no-pinshared no-zlib` and packages generated headers,
libssl.a, libcrypto.a, the license and a per-platform build manifest.

Commit the tarball, both complete platform output directories, LICENSE.txt,
and this script when preparing the offline release. These files are optional
for `--online`: that mode instead compiles Kaminowaku against system-managed
OpenSSL 3 and may acquire missing development dependencies using apt or pkg
**only when the operator explicitly requests online installation**.
The default `--offline` installer never downloads anything.

Online package updates do not require adding another OpenSSL release to this
directory, provided its shared-library ABI stays compatible. A future offline
security refresh still requires native static OpenSSL and Kaminowaku rebuilds.

Platform payload:
- `linux/include/openssl/` and `linux/lib/{libssl.a,libcrypto.a}`
- `freebsd/include/openssl/` and `freebsd/lib/{libssl.a,libcrypto.a}`
- `linux/BUILD-MANIFEST.txt` and `freebsd/BUILD-MANIFEST.txt`
- `source/openssl-3.5.8.tar.gz`
- `LICENSE.txt` — OpenSSL Apache-2.0 license

Generated configuration.h is platform-specific: do not reuse the Linux
header on FreeBSD. CA trust remains an OS/user runtime prerequisite; do not
disable TLS certificate validation when no trust store exists. Static
bundling does not imply FIPS validation.
