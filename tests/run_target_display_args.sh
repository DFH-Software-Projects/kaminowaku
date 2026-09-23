#!/bin/sh
# Test the project display argument grammar without NOSIX or network access.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TMPDIR_TEST=$(mktemp -d)
trap 'rm -rf "$TMPDIR_TEST"' EXIT HUP INT TERM
: "${CC:=cc}"

"$CC" -D_POSIX_C_SOURCE=200809L -std=c11 -Wall -Wextra -Werror -Wpedantic \
        -I"$ROOT/src/network/scan/ports" -I"$ROOT/src/targets" \
        "$ROOT/src/network/scan/ports/kportspec.c" \
        "$ROOT/src/targets/ktargetdisplay_args.c" \
        "$ROOT/tests/test_ktargetdisplay_args.c" \
        -o "$TMPDIR_TEST/test_ktargetdisplay_args"
"$TMPDIR_TEST/test_ktargetdisplay_args"
