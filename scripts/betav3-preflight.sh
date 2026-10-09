#!/bin/sh
# Copyright 2026 Jamison A. Drapeau
# Read-only BetaV3 ABI staging/release gate. Never builds, installs or merges.
set -eu

mode=${1:---release}
case "$mode" in
    --staging|--release) ;;
    *)
        echo "Usage: sh scripts/betav3-preflight.sh [--staging|--release]" >&2
        exit 2
        ;;
esac

root=$(CDPATH= cd "$(dirname "$0")/.." && pwd)
headers="$root/libs/nosix/include/nosix.h"
manifest="$root/libs/nosix/BUILD-MANIFEST.txt"
license="$root/libs/nosix/LICENSE"

fail() {
    echo "[x] BetaV3 preflight: $*" >&2
    exit 1
}

[ -s "$headers" ] || fail "Missing NOSIX public header"
[ -s "$manifest" ] || fail "Missing NOSIX build manifest"
[ -s "$license" ] || fail "Missing packaged NOSIX license"

grep -Eq '^#define[[:space:]]+NOSIX_ABI_VERSION_MAJOR[[:space:]]+1U([[:space:]]|$)' "$headers" ||
    fail "Expected NOSIX ABI 1.x header"
grep -Eq '^#define[[:space:]]+NOSIX_ABI_VERSION_MINOR[[:space:]]+5U([[:space:]]|$)' "$headers" ||
    fail "Expected NOSIX ABI 1.5 header"

for platform in linux freebsd; do
    env_file="$root/libs/nosix/$platform/abi.env"
    [ -s "$env_file" ] || fail "Missing $platform ABI metadata"
    platform_value=$(sed -n 's/^PLATFORM=//p' "$env_file")
    arch_value=$(sed -n 's/^ARCH=//p' "$env_file")
    linker_value=$(sed -n 's/^LINKER_NAME=//p' "$env_file")
    soname_value=$(sed -n 's/^SONAME_NAME=//p' "$env_file")
    real_value=$(sed -n 's/^REAL_NAME=//p' "$env_file")
    [ "$platform_value" = "$platform" ] || fail "$platform ABI platform mismatch"
    [ "$arch_value" = amd64 ] || fail "$platform ABI architecture must be amd64"
    [ "$linker_value" = libnosix.so ] || fail "$platform linker name mismatch"
    [ "$soname_value" = libnosix.so.1 ] || fail "$platform SONAME mismatch"

    case "$real_value" in
        libnosix.so.1.5.0)
            [ -s "$root/libs/nosix/$platform/lib/$real_value" ] ||
                fail "$platform ABI 1.5.0 library missing or empty"
            echo "[+] $platform native ABI 1.5.0 present"
            ;;
        libnosix.so.1.4.0)
            [ "$mode" = --staging ] ||
                fail "$platform still contains ABI 1.4.0; repackage native NOSIX/BetaV3 before deployment"
            [ -s "$root/libs/nosix/$platform/lib/$real_value" ] ||
                fail "$platform staged ABI 1.4.0 library missing or empty"
            echo "[i] $platform ABI 1.4.0 is staging-only (native 1.5.0 required before testing)"
            ;;
        *)
            fail "$platform unexpected NOSIX real library name: $real_value"
            ;;
    esac
done

echo "[+] BetaV3 ${mode#--} preflight passed; this is metadata validation, not a native runtime test."
