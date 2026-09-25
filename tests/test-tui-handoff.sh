#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT="${TMPDIR:-/tmp}/kami-tui-handoff-$$"
trap 'rm -f "$OUT"' EXIT HUP INT TERM

"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pthread \
        -I"$ROOT/src/ui" -I"$ROOT/src" -I"$ROOT/libs/nosix/include" \
        "$ROOT/src/ui/ui_events.c" "$ROOT/tests/tui_phase4_handoff.c" \
        -o "$OUT"
"$OUT"
