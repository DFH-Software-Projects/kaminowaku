# Packaged OpenSSL

Kaminowaku pins **OpenSSL 3.5.8 LTS** (Apache License 2.0). The release
archive contains the verified upstream source tarball and **native, static**
OpenSSL libraries for Linux and FreeBSD amd64. The target machine never
downloads OpenSSL, touches a package repository, or installs an OpenSSL
library into system locations.

## Prepare on the build machines

Run `./libs/openssl/vendor.sh fetch` **once on a connected staging host**.
Distribute the resulting `openssl-3.5.8.tar.gz` to both Linux and FreeBSD
build machines. On **each native machine**, run:

```sh
./libs/openssl/vendor.sh build
```

The build machine needs clang (or a configured CC), make and Perl. The
restricted installation target needs only clang and make, not Perl, curl,
pkg-config or system OpenSSL development headers.

The native build writes `libs/openssl/linux/` or
`libs/openssl/freebsd/`, containing generated headers and
`libssl.a` / `libcrypto.a`. Both native builds and the source tarball
must be present when assembling a release with `./release.sh`.

OpenSSL is statically linked into the Kaminowaku executable. The
upstream source tarball and original `LICENSE.txt` are included in every
release and the license is installed alongside the other component licenses.
The system trust store is still used for TLS certificate verification.

The vendored source checksum is in [SHA256](SHA256). Bump the version,
checksum, rebuild **both** native archives, review the upstream security
notes and retest before updating a release.
