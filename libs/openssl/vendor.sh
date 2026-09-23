#!/bin/sh
# Copyright 2026 Jamison A. Drapeau
# Developer-side preparation only. The end-user installer NEVER downloads.

set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
VERSION="3.5.8"
ARCHIVE="$ROOT/openssl-$VERSION.tar.gz"
EXPECTED=$(awk 'NR==1 {print $1}' "$ROOT/SHA256")
SOURCE_URL="https://github.com/openssl/openssl/releases/download/openssl-$VERSION/openssl-$VERSION.tar.gz"
ACTION=${1:-build}

fail() { echo "ERROR: $*" >&2; exit 1; }

sha256_file() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | awk '{print $1}'
    elif command -v sha256 >/dev/null 2>&1; then
        sha256 -q "$1"
    elif command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$1" | awk '{print $1}'
    else
        fail "SHA256 tool missing (sha256sum, sha256, or shasum required)."
    fi
}

verify_archive() {
    [ -f "$ARCHIVE" ] || fail "OpenSSL source missing: $ARCHIVE; run vendor.sh fetch on a connected build host."
    ACTUAL=$(sha256_file "$ARCHIVE")
    [ "$ACTUAL" = "$EXPECTED" ] || fail "OpenSSL checksum mismatch; archive rejected."
    echo "[openssl] verified OpenSSL $VERSION upstream SHA256"
}

fetch_archive() {
    if [ -f "$ARCHIVE" ]; then verify_archive; return; fi
    TMP="$ARCHIVE.partial.$$"
    trap 'rm -f "$TMP"' EXIT HUP INT TERM
    if command -v curl >/dev/null 2>&1; then
        curl --fail --location --retry 3 --output "$TMP" "$SOURCE_URL" || fail "OpenSSL download failed."
    elif command -v fetch >/dev/null 2>&1; then
        fetch -o "$TMP" "$SOURCE_URL" || fail "OpenSSL download failed."
    else
        fail "curl or fetch is required on the connected build host."
    fi
    mv "$TMP" "$ARCHIVE"
    trap - EXIT HUP INT TERM
    verify_archive
}

build_native() {
    verify_archive
    command -v perl >/dev/null 2>&1 || fail "Perl is required only on the build host."
    command -v make >/dev/null 2>&1 || fail "make not found on the build host."
    case "$(uname -s):$(uname -m)" in
        Linux:x86_64|Linux:amd64) PLATFORM=linux ;;
        FreeBSD:x86_64|FreeBSD:amd64) PLATFORM=freebsd ;;
        *) fail "This release builds only native Linux/FreeBSD amd64 OpenSSL." ;;
    esac
    WORK=$(mktemp -d "${TMPDIR:-/tmp}/kami-openssl.XXXXXX")
    trap 'rm -rf "$WORK"' EXIT HUP INT TERM
    tar -xzf "$ARCHIVE" -C "$WORK"
    SOURCE="$WORK/openssl-$VERSION"
    [ -f "$SOURCE/LICENSE.txt" ] || fail "Missing upstream OpenSSL license."
    DEST="$ROOT/$PLATFORM"
    PREFIX="/usr/local"
    (
        cd "$SOURCE"
        # Compiled default CA directory is /etc/ssl; DESTDIR keeps install local.
        CC="${CC:-clang}" ./config no-shared no-module no-tests \
            --prefix="$PREFIX" --openssldir=/etc/ssl --libdir=lib
        make ${JOBS:+-j "$JOBS"}
        make DESTDIR="$WORK/dest" install_dev
    )
    [ -f "$WORK/dest$PREFIX/lib/libssl.a" ] || fail "OpenSSL static TLS library missing."
    [ -f "$WORK/dest$PREFIX/lib/libcrypto.a" ] || fail "OpenSSL static crypto library missing."
    rm -rf "$DEST"
    mkdir -p "$DEST/include" "$DEST/lib"
    cp -R "$WORK/dest$PREFIX/include/." "$DEST/include/"
    cp "$WORK/dest$PREFIX/lib/libssl.a" "$WORK/dest$PREFIX/lib/libcrypto.a" "$DEST/lib/"
    cp "$SOURCE/LICENSE.txt" "$ROOT/LICENSE.txt"
    printf 'OPENSSL_VERSION=%s\nPLATFORM=%s\nARCH=amd64\nSHA256=%s\n' \
        "$VERSION" "$PLATFORM" "$EXPECTED" > "$DEST/abi.env"
    echo "[openssl] $PLATFORM/amd64 static libraries ready at $DEST"
}

case "$ACTION" in
    fetch) fetch_archive ;;
    verify) verify_archive ;;
    build) build_native ;;
    *) fail "Usage: $0 [fetch|verify|build]" ;;
esac
