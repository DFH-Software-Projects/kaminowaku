#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT="${TMPDIR:-/tmp}/kami-tui-input-$$"
trap 'rm -f "$OUT"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -I"$ROOT/src/ui" \
        "$ROOT/src/ui/kio_escape.c" "$ROOT/tests/tui_input_regression.c" -o "$OUT"
"$OUT"
