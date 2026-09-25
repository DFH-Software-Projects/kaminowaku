#!/bin/sh
# @@ Performance numbers are diagnostic, not machine-dependent pass thresholds.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT="${TMPDIR:-/tmp}/kami-tui-perf-$$"
trap 'rm -f "$OUT"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Werror \
        -I"$ROOT/src/ui" -I"$ROOT/src" -I"$ROOT/libs/nosix/include" \
        "$ROOT/src/ui/ui_screen.c" "$ROOT/src/ui/ui_wrap_index.c" \
        "$ROOT/tests/tui_phase5_perf.c" -o "$OUT"
"$OUT"
