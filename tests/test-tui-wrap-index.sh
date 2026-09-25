#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT="${TMPDIR:-/tmp}/kami-tui-index-$$"
trap 'rm -f "$OUT"' EXIT HUP INT TERM

"${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -I"$ROOT/src" -I"$ROOT/src/ui" -I"$ROOT/libs/nosix/include" \
        "$ROOT/src/ui/ui_wrap_index.c" "$ROOT/tests/tui_phase3_wrap_index.c" \
        -o "$OUT"
"$OUT"
