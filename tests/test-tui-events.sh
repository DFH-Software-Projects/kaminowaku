#!/bin/sh
# @@ Standalone Linux/BSD Phase 1 regression. No application binary required.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT="${TMPDIR:-/tmp}/kami-tui-events-$$"
trap 'rm -f "$OUT"' EXIT HUP INT TERM

"${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -I"$ROOT/src/ui" -I"$ROOT/src" -I"$ROOT/libs/nosix/include" \
        "$ROOT/src/ui/ui_events.c" "$ROOT/tests/tui_phase1_events.c" \
        -o "$OUT"
"$OUT"
