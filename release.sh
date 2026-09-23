#!/bin/sh
# Copyright 2026 Jamison A. Drapeau
# Build-host release assembly. Produces an offline-installable release tree.

set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
OUT="${OUT:-$ROOT/kaminowaku-release}"
VERSION=3.5.8
ARCHIVE="$ROOT/libs/openssl/openssl-$VERSION.tar.gz"
fail() { echo "ERROR: $*" >&2; exit 1; }

[ -d "$ROOT/.git" ] || fail "Run release.sh from a git checkout."
[ -f "$ARCHIVE" ] || fail "Vendor OpenSSL on the connected build machine first."
"$ROOT/libs/openssl/vendor.sh" verify

for PLATFORM in linux freebsd; do
    SRC="$ROOT/libs/openssl/$PLATFORM"
    [ -f "$SRC/abi.env" ] || fail "Missing native OpenSSL metadata for $PLATFORM."
    [ -f "$SRC/lib/libssl.a" ] || fail "Missing native OpenSSL libssl.a for $PLATFORM."
    [ -f "$SRC/lib/libcrypto.a" ] || fail "Missing native OpenSSL libcrypto.a for $PLATFORM."
    [ -f "$SRC/include/openssl/ssl.h" ] || fail "Missing native OpenSSL headers for $PLATFORM."
    grep -qx "OPENSSL_VERSION=$VERSION" "$SRC/abi.env" || fail "Wrong OpenSSL version for $PLATFORM."
    grep -qx "PLATFORM=$PLATFORM" "$SRC/abi.env" || fail "Wrong OpenSSL platform for $PLATFORM."
done
[ -f "$ROOT/libs/openssl/LICENSE.txt" ] || fail "Missing original OpenSSL license."

if [ -f "$ROOT/libs/nosix/LICENSE" ]; then
    NOSIX="$ROOT/libs/nosix"
else
    NOSIX="$ROOT/nosix_abi"
fi
[ -f "$NOSIX/BUILD-MANIFEST.txt" ] || fail "Missing packaged NOSIX build manifest."
[ -f "$NOSIX/LICENSE" ] || fail "Missing packaged NOSIX license."
for PLATFORM in linux freebsd; do
    [ -f "$NOSIX/$PLATFORM/abi.env" ] || fail "Missing NOSIX $PLATFORM ABI metadata."
done

[ "$OUT" != "$ROOT" ] || fail "OUT must not point at the repository root."
[ ! -e "$OUT" ] || fail "Release output exists: $OUT (move or remove it explicitly)."
mkdir -p "$OUT"
# git archive excludes local build products and prevents recursively copying OUT.
git -C "$ROOT" archive --format=tar HEAD | (cd "$OUT" && tar -xf -)
rm -rf "$OUT/nosix_abi"
mkdir -p "$OUT/libs/nosix"
cp -R "$NOSIX/." "$OUT/libs/nosix/"
cp "$ARCHIVE" "$OUT/libs/openssl/"
cp "$ROOT/libs/openssl/LICENSE.txt" "$OUT/libs/openssl/"
for PLATFORM in linux freebsd; do
    cp -R "$ROOT/libs/openssl/$PLATFORM" "$OUT/libs/openssl/"
done
echo "[release] Offline release assembled at $OUT"
echo "[release] Transfer the entire directory; run ./install.sh check, then sudo ./install.sh install."
