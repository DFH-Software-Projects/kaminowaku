#!/bin/sh
# Copyright 2026 Jamison A. Drapeau
# Developer/release-host operation only. Offline install.sh never invokes this.
set -eu

HERE=$(CDPATH= cd "$(dirname "$0")" && pwd)
ROOT=$(CDPATH= cd "$HERE/../.." && pwd)
VERSION=3.5.8
SOURCE_SHA256=a8f84a39918ec6415ce765d9b429d313ba97b8143169c172e734b9514464f5b2
SOURCE_TARBALL="$HERE/source/openssl-$VERSION.tar.gz"
JOBS="${JOBS:-2}"

die() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }
hash_file() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | cut -d ' ' -f 1
    elif command -v sha256 >/dev/null 2>&1; then
        sha256 -q "$1"
    else
        die "A SHA-256 checksum utility is required."
    fi
}
for tool in clang make perl tar install; do
    command -v "$tool" >/dev/null 2>&1 || die "$tool missing on RELEASE BUILD host."
done
case "$(uname -s)" in
    Linux) PLATFORM=linux; CONFIG_TARGET=linux-x86_64 ;;
    FreeBSD) PLATFORM=freebsd; CONFIG_TARGET=BSD-x86_64 ;;
    *) die "Only Linux and FreeBSD are packaged." ;;
esac
case "$(uname -m)" in
    amd64|x86_64) ARCH=amd64 ;;
    *) die "Only amd64 native OpenSSL builds are packaged." ;;
esac
[ -f "$SOURCE_TARBALL" ] || die "Put verified official OpenSSL source at $SOURCE_TARBALL"
[ "$(hash_file "$SOURCE_TARBALL")" = "$SOURCE_SHA256" ] || die "OpenSSL 3.5.8 source SHA-256 mismatch."
WORK="$ROOT/.STAGE/openssl-$PLATFORM-$ARCH"
BUILD="$WORK/openssl-$VERSION"
DEST="$WORK/install"
OUTPUT="$HERE/$PLATFORM"
rm -rf "$WORK"
mkdir -p "$WORK"
tar -xzf "$SOURCE_TARBALL" -C "$WORK"
[ -f "$BUILD/LICENSE.txt" ] || die "Verified source archive did not unpack as expected."
cd "$BUILD"
# no-module embeds standard/default providers; no-shared/no-pinshared avoid
# an external libssl/libcrypto provider runtime dependency.
CC=clang ./Configure "$CONFIG_TARGET" no-shared no-module no-pinshared \
    no-zlib --prefix="$DEST" --libdir=lib --openssldir=/etc/ssl
make -j "$JOBS"
make test
make install_sw
[ -s "$DEST/lib/libssl.a" ] || die "libssl.a not generated."
[ -s "$DEST/lib/libcrypto.a" ] || die "libcrypto.a not generated."
[ -s "$DEST/include/openssl/configuration.h" ] || die "Generated OpenSSL configuration.h missing."
mkdir -p "$OUTPUT/include" "$OUTPUT/lib"
rm -rf "$OUTPUT/include/openssl"
cp -R "$DEST/include/openssl" "$OUTPUT/include/"
cp "$DEST/lib/libssl.a" "$DEST/lib/libcrypto.a" "$OUTPUT/lib/"
cp "$BUILD/LICENSE.txt" "$HERE/LICENSE.txt"
SOURCE_ACTUAL=$(hash_file "$SOURCE_TARBALL")
SSL_SHA=$(hash_file "$OUTPUT/lib/libssl.a")
CRYPTO_SHA=$(hash_file "$OUTPUT/lib/libcrypto.a")
cat > "$OUTPUT/BUILD-MANIFEST.txt" <<EOF
VERSION=$VERSION
PLATFORM=$PLATFORM
ARCH=$ARCH
SOURCE_SHA256=$SOURCE_ACTUAL
CONFIGURE_TARGET=$CONFIG_TARGET
CONFIGURE_FLAGS=no-shared,no-module,no-pinshared,no-zlib
LIBSSL_SHA256=$SSL_SHA
LIBCRYPTO_SHA256=$CRYPTO_SHA
EOF
printf '[+] Native OpenSSL %s ready in %s\n' "$VERSION" "$OUTPUT"
printf '[i] Commit the platform include/, lib/, BUILD-MANIFEST.txt, shared LICENSE.txt,\n'
printf '    and the verified source tarball before generating the offline release.\n'
