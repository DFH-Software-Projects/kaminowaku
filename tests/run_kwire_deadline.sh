#!/bin/sh
# Standalone POSIX test runner for absolute scanner deadlines.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TMPDIR_TEST=$(mktemp -d)
trap 'rm -rf "$TMPDIR_TEST"' EXIT HUP INT TERM
: "${CC:=cc}"

"$CC" -D_POSIX_C_SOURCE=200809L -std=c11 -Wall -Wextra -Werror -Wpedantic \
        -I"$ROOT/src/network/scan" \
        "$ROOT/tests/test_kwire_deadline.c" \
        -o "$TMPDIR_TEST/test_kwire_deadline"
"$TMPDIR_TEST/test_kwire_deadline"
