#!/bin/sh
# Copyright 2026 Jamison A. Drapeau
# Beta 0.3.1 cross-file release-version and dependency lock guard.
# Update the expected version intentionally at the next release cycle.
set -eu
ROOT=$(CDPATH= cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
fail() { printf '[x] Release version check: %s\n' "$*" >&2; exit 1; }
contains() {
    grep -Fq -- "$2" "$1" || fail "$1 does not contain required value: $2"
}

EXPECTED_VERSION="0.3.1"
APP_VERSION=$(sed -n 's/^#define[[:space:]]*VERSION[[:space:]]*"\([^"]*\)".*/\1/p' src/data.h)
[ "$APP_VERSION" = "$EXPECTED_VERSION" ] ||
    fail "src/data.h VERSION=$APP_VERSION; expected $EXPECTED_VERSION"

contains README.md "**Kaminowaku $APP_VERSION**"
contains CHANGELOG.md "## $APP_VERSION — Beta"
contains SECURITY.md "| $APP_VERSION (current beta;"
contains docs/BOOK_LANGUAGE_V1.md "Kaminowaku $APP_VERSION."
contains docs/LINK_GRAPH.md "Kaminowaku $APP_VERSION beta implementation"
contains release/README.md "Kaminowaku **$APP_VERSION**"

LOCK="libs/openssl/source/openssl.lock"
[ -s "$LOCK" ] || fail "Pinned OpenSSL source lock missing"
OPENSSL_VERSION=$(sed -n 's/^VERSION=//p' "$LOCK" | tr -d '\r')
OPENSSL_SHA=$(sed -n 's/^SOURCE_SHA256=//p' "$LOCK" | tr -d '\r' | tr '[:upper:]' '[:lower:]')
printf '%s\n' "$OPENSSL_VERSION" | grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+$' ||
    fail "OpenSSL lock version is malformed"
printf '%s\n' "$OPENSSL_SHA" | grep -Eq '^[0-9a-f]{64}$' ||
    fail "OpenSSL lock SHA-256 is malformed"
[ -s "libs/openssl/source/openssl-$OPENSSL_VERSION.tar.gz" ] ||
    fail "Pinned OpenSSL archive is missing"

contains README.md "OpenSSL $OPENSSL_VERSION native release"
contains libs/openssl/README.md "# Vendored OpenSSL $OPENSSL_VERSION"
contains libs/openssl/README.md "$OPENSSL_SHA"
contains docs/LINK_GRAPH.md "OpenSSL $OPENSSL_VERSION source"
contains release/README.md "OpenSSL **$OPENSSL_VERSION**"
for platform in linux freebsd; do
    manifest="libs/openssl/$platform/BUILD-MANIFEST.txt"
    contains "$manifest" "VERSION=$OPENSSL_VERSION"
    contains "$manifest" "SOURCE_SHA256=$OPENSSL_SHA"
    [ -s "libs/openssl/$platform/lib/libssl.a" ] ||
        fail "Missing pinned $platform libssl.a"
    [ -s "libs/openssl/$platform/lib/libcrypto.a" ] ||
        fail "Missing pinned $platform libcrypto.a"
done

contains libs/nosix/README.md "lib/libnosix.so.1.5.0"
contains release/README.md "NOSIX ABI **1.5.0**"
contains libs/nosix/BUILD-MANIFEST.txt "Linux real library: libnosix.so.1.5.0"
contains libs/nosix/BUILD-MANIFEST.txt "FreeBSD real library: libnosix.so.1.5.0"
for platform in linux freebsd; do
    contains "libs/nosix/$platform/abi.env" "REAL_NAME=libnosix.so.1.5.0"
    [ -s "libs/nosix/$platform/lib/libnosix.so.1.5.0" ] ||
        fail "Missing $platform NOSIX 1.5.0 runtime"
done

# Historical changelog entries and archived BetaV3 design notes are exempt.
for path in README.md Makefile install.sh libs/nosix/README.md \
    libs/openssl/README.md release/README.md release/package-native.sh \
    docs/LINK_GRAPH.md; do
    if grep -En 'OpenSSL 3\.5\.8|openssl-3\.5\.8|libnosix\.so\.1\.4\.0' "$path"; then
        fail "Stale native dependency version in $path"
    fi
done

sh -n install.sh
sh -n release/package-native.sh
sh -n uninstall.sh
printf '[+] Kaminowaku %s version metadata and native dependency pins verified.\n' "$APP_VERSION"
