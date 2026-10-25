#!/bin/sh
# Copyright 2026 Jamison A. Drapeau
# Developer/release-host operation only: package the locally built binary.
set -eu
HERE=$(CDPATH= cd "$(dirname "$0")" && pwd)
ROOT=$(CDPATH= cd "$HERE/.." && pwd)
cd "$ROOT"
fail() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }
hash_file() {
    if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -d ' ' -f 1
    elif command -v sha256 >/dev/null 2>&1; then sha256 -q "$1"
    else fail "SHA-256 utility not found."; fi
}
case "$(uname -s)" in
    Linux) PLATFORM=linux ;;
    FreeBSD) PLATFORM=freebsd ;;
    *) fail "Linux or FreeBSD is required." ;;
esac
case "$(uname -m)" in amd64|x86_64) ARCH=amd64 ;; *) fail "amd64 only." ;; esac
[ -s .STAGE/bin/kaminowaku ] || fail "Run './install.sh all BUILD=release --offline' on this machine first."
[ -s .STAGE/meta/build-mode.env ] || fail "Missing .STAGE build mode; rebuild with ./install.sh all BUILD=release --offline."
grep -Fx 'OPENSSL_MODE=offline' .STAGE/meta/build-mode.env >/dev/null     || fail "Cannot package a system-linked --online executable as an offline release. Rebuild with --offline."
[ -s ".STAGE/lib/kaminowaku/libnosix.so.1.5.0" ] || fail "Staged NOSIX library is missing."
[ -s "libs/openssl/$PLATFORM/BUILD-MANIFEST.txt" ] || fail "Native OpenSSL package missing."
OPENSSL_LOCK="libs/openssl/source/openssl.lock"
[ -s "$OPENSSL_LOCK" ] || fail "Pinned OpenSSL lock missing."
OPENSSL_VERSION=$(sed -n 's/^VERSION=//p' "$OPENSSL_LOCK" | tr -d '\r')
OPENSSL_PIN=$(sed -n 's/^SOURCE_SHA256=//p' "$OPENSSL_LOCK" | tr -d '\r' | tr '[:upper:]' '[:lower:]')
printf '%s\n' "$OPENSSL_VERSION" | grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+
if command -v ldd >/dev/null 2>&1; then
    linkage=$(ldd .STAGE/bin/kaminowaku 2>&1) || fail "Built binary has missing runtime dependencies: $linkage"
    printf '%s\n' "$linkage" | grep 'not found' >/dev/null 2>&1 && fail "Built binary has unresolved dependencies."
    printf '%s\n' "$linkage" | grep 'libssl.so\|libcrypto.so' >/dev/null 2>&1 && fail "Unexpected shared system OpenSSL linkage."
    printf '%s\n' "$linkage" | grep 'libnosix.so.1' >/dev/null 2>&1 || fail "Staged NOSIX was not linked."
fi
SOURCE_VERSION=$(sed -n 's/^[[:space:]]*#define[[:space:]]*VERSION[[:space:]]*"\([^"]*\)".*/\1/p' src/data.h)
[ -n "$SOURCE_VERSION" ] || fail "Kaminowaku source VERSION is missing."
sh scripts/check-release-versions.sh
OUTPUT="$HERE/$PLATFORM-$ARCH"
mkdir -p "$OUTPUT/bin"
install -m 755 .STAGE/bin/kaminowaku "$OUTPUT/bin/kaminowaku"
BINARY_SHA256=$(hash_file "$OUTPUT/bin/kaminowaku")
NOSIX_SHA256=$(hash_file "libs/nosix/$PLATFORM/lib/libnosix.so.1.5.0")
OPENSSL_SOURCE_SHA256=$(hash_file "$OPENSSL_ARCHIVE")
[ "$OPENSSL_SOURCE_SHA256" = "$OPENSSL_PIN" ] || fail "OpenSSL source archive did not match the checked-in lock."
cat > "$OUTPUT/BUILD-MANIFEST.txt" <<EOF
PLATFORM=$PLATFORM
ARCH=$ARCH
BUILD=release
VERSION=$SOURCE_VERSION
BINARY_SHA256=$BINARY_SHA256
NOSIX_SHA256=$NOSIX_SHA256
OPENSSL_SOURCE_SHA256=$OPENSSL_SOURCE_SHA256
EOF
printf '[+] Prepared offline native release: %s\n' "$OUTPUT"
printf '[i] Copy both Linux and FreeBSD native release directories into\n'
printf '    the final Kaminowaku release checkout before distributing.\n'
 || fail "Invalid pinned OpenSSL version."
printf '%s\n' "$OPENSSL_PIN" | grep -Eq '^[0-9a-f]{64}
if command -v ldd >/dev/null 2>&1; then
    linkage=$(ldd .STAGE/bin/kaminowaku 2>&1) || fail "Built binary has missing runtime dependencies: $linkage"
    printf '%s\n' "$linkage" | grep 'not found' >/dev/null 2>&1 && fail "Built binary has unresolved dependencies."
    printf '%s\n' "$linkage" | grep 'libssl.so\|libcrypto.so' >/dev/null 2>&1 && fail "Unexpected shared system OpenSSL linkage."
    printf '%s\n' "$linkage" | grep 'libnosix.so.1' >/dev/null 2>&1 || fail "Staged NOSIX was not linked."
fi
SOURCE_VERSION=$(sed -n 's/^[[:space:]]*#define[[:space:]]*VERSION[[:space:]]*"\([^"]*\)".*/\1/p' src/data.h)
[ -n "$SOURCE_VERSION" ] || fail "Kaminowaku source VERSION is missing."
OUTPUT="$HERE/$PLATFORM-$ARCH"
mkdir -p "$OUTPUT/bin"
install -m 755 .STAGE/bin/kaminowaku "$OUTPUT/bin/kaminowaku"
BINARY_SHA256=$(hash_file "$OUTPUT/bin/kaminowaku")
NOSIX_SHA256=$(hash_file "libs/nosix/$PLATFORM/lib/libnosix.so.1.5.0")
OPENSSL_SOURCE_SHA256=$(hash_file "libs/openssl/source/openssl-3.5.8.tar.gz")
[ "$OPENSSL_SOURCE_SHA256" = "a8f84a39918ec6415ce765d9b429d313ba97b8143169c172e734b9514464f5b2" ] || fail "OpenSSL source archive did not match the pinned official checksum."
cat > "$OUTPUT/BUILD-MANIFEST.txt" <<EOF
PLATFORM=$PLATFORM
ARCH=$ARCH
BUILD=release
VERSION=$SOURCE_VERSION
BINARY_SHA256=$BINARY_SHA256
NOSIX_SHA256=$NOSIX_SHA256
OPENSSL_SOURCE_SHA256=$OPENSSL_SOURCE_SHA256
EOF
printf '[+] Prepared offline native release: %s\n' "$OUTPUT"
printf '[i] Copy both Linux and FreeBSD native release directories into\n'
printf '    the final Kaminowaku release checkout before distributing.\n'
 || fail "Invalid pinned OpenSSL checksum."
OPENSSL_ARCHIVE="libs/openssl/source/openssl-$OPENSSL_VERSION.tar.gz"
[ -s "$OPENSSL_ARCHIVE" ] || fail "Vendored OpenSSL source archive missing."
grep -Fx "VERSION=$OPENSSL_VERSION" "libs/openssl/$PLATFORM/BUILD-MANIFEST.txt" >/dev/null || fail "OpenSSL manifest does not match pinned version."
grep -Fx "SOURCE_SHA256=$OPENSSL_PIN" "libs/openssl/$PLATFORM/BUILD-MANIFEST.txt" >/dev/null || fail "OpenSSL manifest does not match pinned checksum."
if command -v ldd >/dev/null 2>&1; then
    linkage=$(ldd .STAGE/bin/kaminowaku 2>&1) || fail "Built binary has missing runtime dependencies: $linkage"
    printf '%s\n' "$linkage" | grep 'not found' >/dev/null 2>&1 && fail "Built binary has unresolved dependencies."
    printf '%s\n' "$linkage" | grep 'libssl.so\|libcrypto.so' >/dev/null 2>&1 && fail "Unexpected shared system OpenSSL linkage."
    printf '%s\n' "$linkage" | grep 'libnosix.so.1' >/dev/null 2>&1 || fail "Staged NOSIX was not linked."
fi
SOURCE_VERSION=$(sed -n 's/^[[:space:]]*#define[[:space:]]*VERSION[[:space:]]*"\([^"]*\)".*/\1/p' src/data.h)
[ -n "$SOURCE_VERSION" ] || fail "Kaminowaku source VERSION is missing."
OUTPUT="$HERE/$PLATFORM-$ARCH"
mkdir -p "$OUTPUT/bin"
install -m 755 .STAGE/bin/kaminowaku "$OUTPUT/bin/kaminowaku"
BINARY_SHA256=$(hash_file "$OUTPUT/bin/kaminowaku")
NOSIX_SHA256=$(hash_file "libs/nosix/$PLATFORM/lib/libnosix.so.1.5.0")
OPENSSL_SOURCE_SHA256=$(hash_file "libs/openssl/source/openssl-3.5.8.tar.gz")
[ "$OPENSSL_SOURCE_SHA256" = "a8f84a39918ec6415ce765d9b429d313ba97b8143169c172e734b9514464f5b2" ] || fail "OpenSSL source archive did not match the pinned official checksum."
cat > "$OUTPUT/BUILD-MANIFEST.txt" <<EOF
PLATFORM=$PLATFORM
ARCH=$ARCH
BUILD=release
VERSION=$SOURCE_VERSION
BINARY_SHA256=$BINARY_SHA256
NOSIX_SHA256=$NOSIX_SHA256
OPENSSL_SOURCE_SHA256=$OPENSSL_SOURCE_SHA256
EOF
printf '[+] Prepared offline native release: %s\n' "$OUTPUT"
printf '[i] Copy both Linux and FreeBSD native release directories into\n'
printf '    the final Kaminowaku release checkout before distributing.\n'
