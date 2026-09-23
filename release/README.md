# Offline native release binaries

A release checkout can install without make, clang, Perl, or an available
package repository when these files have been prepared natively:

- `release/linux-amd64/bin/kaminowaku` and its BUILD-MANIFEST.txt
- `release/freebsd-amd64/bin/kaminowaku` and its BUILD-MANIFEST.txt

On each host, first build the matching static OpenSSL package from the pinned
source archive using `libs/openssl/build-native.sh`, then run
`./install.sh all BUILD=release` and `./release/package-native.sh`.
Copy the resulting native directories together into the release checkout.

The installer checks the selected native executable's SHA-256 and platform
before installing. It copies the versioned NOSIX library exclusively to
`PREFIX/lib/kaminowaku/`; the executable resolves it through its relative
loader path. OpenSSL is linked statically and neither library is placed in a
global system OpenSSL directory.

Compatibility is limited by each native binary's libc and OS baseline.
Build Linux on the oldest intended supported libc and FreeBSD on the oldest
intended supported OS release; record and validate those baselines before
publishing universal claims.

## Offline installation acceptance test

After the official source archive, both native OpenSSL packages, and both
release executable manifests have been committed, deploy the complete Git
tree to the Linux and FreeBSD test hosts. On **each host**, run:

```sh
sudo ./tests/offline-install-smoke.sh
```

The smoke test sets an isolated, automatically removed `PREFIX` and inserts
failure stubs for package managers, downloaders and compilers into `PATH`.
It verifies the SHA-256-validated native binary was installed without a build,
checks bundled assets, and uses `ldd` to require private NOSIX linkage while
rejecting dynamic OpenSSL linkage. It does not overwrite the system-wide
Kaminowaku installation or remove a user's `~/.kaminowaku` data.

Once both OS tests pass, use `sudo ./install.sh install` for the actual
system-wide installation. Run additional TLS/certificate and network
regression tests before merging the release PR.
