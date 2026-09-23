#!/bin/sh
# Validate display port selection without NOSIX or network access.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TMPDIR_TEST=$(mktemp -d)
trap 'rm -rf "$TMPDIR_TEST"' EXIT HUP INT TERM
: "${CC:=cc}"

"$CC" -D_POSIX_C_SOURCE=200809L -std=c11 -Wall -Wextra -Werror -Wpedantic \
        -I"$ROOT/src/network/scan/ports" \
        "$ROOT/src/network/scan/ports/kportspec.c" \
        "$ROOT/src/network/scan/ports/kportfilter.c" \
        "$ROOT/tests/test_kportfilter.c" \
        -o "$TMPDIR_TEST/test_kportfilter"
"$TMPDIR_TEST/test_kportfilter"
