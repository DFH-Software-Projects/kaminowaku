#!/bin/sh
# @@ Permanent cross-platform regression runner; no repository-local binaries.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TMP=$(mktemp -d "${TMPDIR:-/tmp}/kami-tui-regression.XXXXXX")
trap 'rm -rf "$TMP"' EXIT HUP INT TERM
CC=${CC:-cc}
FLAGS="-std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -pthread"
INCLUDES="-I$ROOT/src -I$ROOT/src/ui -I$ROOT/libs/nosix/include"
if [ "${SANITIZE:-0}" = 1 ]; then
        FLAGS="$FLAGS -g -fsanitize=address,undefined -fno-omit-frame-pointer"
fi

run() {
        name=$1
        shift
        echo "==> $name"
        # @@ Include paths are controlled by the repository and never contain whitespace in CI.
        # shellcheck disable=SC2086
        "$CC" $FLAGS $INCLUDES "$@" -o "$TMP/$name"
        "$TMP/$name"
}
run input-parser "$ROOT/src/ui/kio_escape.c" "$ROOT/tests/tui_input_regression.c"
run complete-kio "$ROOT/src/ui/kio_escape.c" "$ROOT/src/ui/kio.c" "$ROOT/tests/tui_kio_regression.c"
run wheel-bursts "$ROOT/src/ui/ui_events.c" "$ROOT/tests/tui_scroll_regression.c"
run queue-fifo "$ROOT/src/ui/ui_events.c" "$ROOT/tests/tui_phase1_events.c"
run screen-diff "$ROOT/src/ui/ui_screen.c" "$ROOT/tests/tui_phase2_screen.c"
run wrap-index "$ROOT/src/ui/ui_wrap_index.c" "$ROOT/tests/tui_phase3_wrap_index.c"
run concurrent-queue "$ROOT/src/ui/ui_events.c" "$ROOT/tests/tui_phase4_queue.c"
run thread-handoff "$ROOT/src/ui/ui_events.c" "$ROOT/tests/tui_phase4_handoff.c"
run pty-contract "$ROOT/tests/tui_phase5_pty.c"
if [ "${1:-}" = "--bench" ]; then
        run ui-benchmark "$ROOT/src/ui/ui_screen.c" "$ROOT/src/ui/ui_wrap_index.c" "$ROOT/tests/tui_phase5_perf.c"
fi
echo "PASS: retained TUI regression suite"
