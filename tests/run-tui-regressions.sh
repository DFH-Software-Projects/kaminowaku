#!/bin/sh
# @@ One entry point for retained Beta V2 TUI regressions.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
for test in \
        test-tui-events.sh \
        test-tui-screen.sh \
        test-tui-wrap-index.sh \
        test-tui-thread-queue.sh \
        test-tui-handoff.sh \
        test-tui-pty.sh
do
        printf "\n==> %s\n" "$test"
        sh "$ROOT/tests/$test"
done
if [ "${1:-}" = "--bench" ]; then
        printf "\n==> TUI performance benchmark\n"
        sh "$ROOT/tests/test-tui-perf.sh"
fi
printf "\nPASS: all TUI regression stages\n"
