#!/bin/sh
# Test the -n target pruning policy without NOSIX runtime or network access.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TMPDIR_TEST=$(mktemp -d)
trap 'rm -rf "$TMPDIR_TEST"' EXIT HUP INT TERM
: "${CC:=cc}"

"$CC" -std=c11 -Wall -Wextra -Werror -Wpedantic \
        -I"$ROOT/libs/nosix/include" -I"$ROOT/src" -I"$ROOT/src/targets" \
        "$ROOT/tests/test_targets_neighbor_filter.c" \
        -o "$TMPDIR_TEST/test_targets_neighbor_filter"
"$TMPDIR_TEST/test_targets_neighbor_filter"
