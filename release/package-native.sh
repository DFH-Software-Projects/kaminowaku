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
[ -s .STAGE/bin/kaminowaku ] || fail "Run './install.sh all BUILD=release' on this machine first."
[ -s ".STAGE/lib/kaminowaku/libnosix.so.1.4.0" ] || fail "Staged NOSIX library is missing."
[ -s "libs/openssl/$PLATFORM/BUILD-MANIFEST.txt" ] || fail "Native OpenSSL package missing."
[ -s "libs/openssl/source/openssl-3.5.8.tar.gz" ] || fail "Vendored OpenSSL source archive missing."
if command -v ldd >/dev/null 2>&1; then
    linkage=$(ldd .STAGE/bin/kaminowaku 2>&1) || fail "Built binary has missing runtime dependencies: $linkage"
    printf '%s\n' "$linkage" | grep 'not found' >/dev/null 2>&1 && fail "Built binary has unresolved dependencies."
    printf '%s\n' "$linkage" | grep 'libssl.so\|libcrypto.so' >/dev/null 2>&1 && fail "Unexpected shared system OpenSSL linkage."
    printf '%s\n' "$linkage" | grep 'libnosix.so.1' >/dev/null 2>&1 || fail "Staged NOSIX was not linked."
fi
OUTPUT="$HERE/$PLATFORM-$ARCH"
mkdir -p "$OUTPUT/bin"
install -m 755 .STAGE/bin/kaminowaku "$OUTPUT/bin/kaminowaku"
BINARY_SHA256=$(hash_file "$OUTPUT/bin/kaminowaku")
NOSIX_SHA256=$(hash_file "libs/nosix/$PLATFORM/lib/libnosix.so.1.4.0")
OPENSSL_SOURCE_SHA256=$(hash_file "libs/openssl/source/openssl-3.5.8.tar.gz")
[ "$OPENSSL_SOURCE_SHA256" = "a8f84a39918ec6415ce765d9b429d313ba97b8143169c172e734b9514464f5b2" ] || fail "OpenSSL source archive did not match the pinned official checksum."
cat > "$OUTPUT/BUILD-MANIFEST.txt" <<EOF
PLATFORM=$PLATFORM
ARCH=$ARCH
BUILD=release
BINARY_SHA256=$BINARY_SHA256
NOSIX_SHA256=$NOSIX_SHA256
OPENSSL_SOURCE_SHA256=$OPENSSL_SOURCE_SHA256
EOF
printf '[+] Prepared offline native release: %s\n' "$OUTPUT"
printf '[i] Copy both Linux and FreeBSD native release directories into\n'
printf '    the final Kaminowaku release checkout before distributing.\n'
