#!/bin/sh
# @@ Keep temporary test products outside the source tree.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT="${TMPDIR:-/tmp}/kami-tui-screen-$$"
trap 'rm -f "$OUT"' EXIT HUP INT TERM

"${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -I"$ROOT/src/ui" \
        "$ROOT/src/ui/ui_screen.c" "$ROOT/tests/tui_phase2_screen.c" \
        -o "$OUT"
"$OUT"
