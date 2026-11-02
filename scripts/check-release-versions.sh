#!/bin/sh
# Copyright 2026 Jamison A. Drapeau
# Kaminowaku 0.3.1 native release/version integrity gate.
set -eu
root=$(CDPATH= cd "$(dirname "$0")/.." && pwd)
cd "$root"
fail() { printf '[x] Release version check: %s\n' "$*" >&2; exit 1; }
has() { grep -Fq -- "$2" "$1" || fail "Expected '$2' in $1"; }

version=$(sed -n 's/^#define[[:space:]]*VERSION[[:space:]]*"\([^"]*\)".*/\1/p' src/data.h)
[ "$version" = "0.3.1" ] || fail "Expected Kaminowaku 0.3.1, found '$version'"
has README.md "**Kaminowaku $version**"
has CHANGELOG.md "## $version — Beta"
has SECURITY.md "| $version (current beta;"
has docs/BOOK_LANGUAGE_V1.md "Kaminowaku $version."
has docs/LINK_GRAPH.md "Kaminowaku $version beta implementation"
has release/README.md "Kaminowaku **$version**"

lock="libs/openssl/source/openssl.lock"
[ -s "$lock" ] || fail "OpenSSL source lock missing"
openssl=$(sed -n 's/^VERSION=//p' "$lock" | tr -d '\r')
pin=$(sed -n 's/^SOURCE_SHA256=//p' "$lock" | tr -d '\r' | tr '[:upper:]' '[:lower:]')
printf '%s\n' "$openssl" | grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+$' || fail "Invalid OpenSSL lock version"
printf '%s\n' "$pin" | grep -Eq '^[0-9a-f]{64}$' || fail "Invalid OpenSSL lock SHA-256"
[ -s "libs/openssl/source/openssl-$openssl.tar.gz" ] || fail "Pinned OpenSSL source archive missing"
has libs/openssl/README.md "# Vendored OpenSSL $openssl"
has libs/openssl/README.md "$pin"
has README.md "OpenSSL $openssl native release"
has release/README.md "OpenSSL **$openssl**"
for platform in linux freebsd; do
    manifest="libs/openssl/$platform/BUILD-MANIFEST.txt"
    has "$manifest" "VERSION=$openssl"
    has "$manifest" "SOURCE_SHA256=$pin"
    [ -s "libs/openssl/$platform/lib/libssl.a" ] || fail "$platform libssl.a missing"
    [ -s "libs/openssl/$platform/lib/libcrypto.a" ] || fail "$platform libcrypto.a missing"

    env="libs/nosix/$platform/abi.env"
    has "$env" "PLATFORM=$platform"
    has "$env" "ARCH=amd64"
    has "$env" "REAL_NAME=libnosix.so.1.5.0"
    [ -s "libs/nosix/$platform/lib/libnosix.so.1.5.0" ] || fail "$platform NOSIX ABI 1.5.0 missing"
done
has release/README.md "NOSIX ABI **1.5.0**"
has libs/nosix/README.md "lib/libnosix.so.1.5.0"
has docs/LINK_GRAPH.md "book_args_parse()"
has docs/LINK_GRAPH.md "book_help_read()"

# No stale file references or temporary laboratory assets in release source.
for removed in docs/BETA_031_LAB.md docs/RELEASE_ACCEPTANCE.md docs/HARD_TIMEOUTS.md \
    scripts/beta031_book_lab.py scripts/betav3-preflight.sh \
    tests/test_beta031_book_lab.py .github/workflows/betav3-l3-compile.yml \
    .github/workflows/beta-031-book-args.yml; do
    [ ! -e "$removed" ] || fail "Redundant release asset exists: $removed"
done
for doc in README.md docs/LINK_GRAPH.md docs/BOOK_LANGUAGE_V1.md \
    release/README.md libs/nosix/README.md; do
    if grep -En 'BETA_031_LAB|RELEASE_ACCEPTANCE\.md|HARD_TIMEOUTS\.md|beta031_book_lab|betav3-preflight|tests/[a-zA-Z0-9_-]+\.(c|lua)' "$doc"; then
        fail "Active document refers to removed file: $doc"
    fi
done
sh -n install.sh
sh -n release/package-native.sh
printf '[+] Kaminowaku %s native release metadata verified.\n' "$version"
