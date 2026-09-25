# Beta V2 TUI regressions

Run all retained terminal/UI regression tests without building or running a
privileged network scan:

```sh
sh tests/run-tui-regressions.sh
```

Add `--bench` to measure unchanged-screen writes, changed-row writes and
scrollback-index costs at the configured maximum number of logical lines.
Benchmarks are diagnostic and have no machine-dependent pass threshold.

All test scripts compile into `${TMPDIR:-/tmp}` and remove their temporary
binaries on exit. CI runs the consolidated suite before attempting the full
Linux build. Keep the underlying individual test sources as permanent
regression coverage rather than retaining separate development copies.

The PTY test validates the operating-system primitives used by interactive
tools: fork/exec, window-size propagation, terminal-attribute restoration,
ANSI bytes and byte-perfect raw output capture. It is **not** a test of the
complete `tool_pty_run()` integration with the running Kaminowaku renderer.
That still requires an interactive application test on Linux and FreeBSD.

The screen and wrap-index benchmarks run against their actual source modules,
not the whole application. Full UI responsiveness under live scanner output
and book/tool execution must be verified separately.
