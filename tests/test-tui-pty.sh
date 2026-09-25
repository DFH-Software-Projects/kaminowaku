#!/bin/sh
# @@ Standalone terminal contract check; no external network or root required.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT="${TMPDIR:-/tmp}/kami-tui-pty-$$"
trap 'rm -f "$OUT"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        "$ROOT/tests/tui_phase5_pty.c" -o "$OUT"
"$OUT"
