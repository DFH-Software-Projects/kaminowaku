# Beta V2 — TUI architecture and display modes

Status: design and migration plan. This document does not imply that the new renderer has been implemented.

## Objectives

Decouple terminal input, command execution, and presentation. Keep the public `kui_add_line()` family available to command handlers, but make it publish output without directly painting the terminal. Give the UI renderer exclusive ownership of terminal writes and viewport state. Improve scroll responsiveness without rescanning and repainting the entire scrollback for every wheel event.

## User-selectable display modes

The renderer will support two views over the same ordered output stream:

- **Continuous** (default): commands and their results accumulate in scrollback. The display follows the tail until the user navigates backward; incoming output must not dislodge a scrolled viewport.
- **Frame**: preserve the existing one-command/one-frame experience. The active command's frame is displayed, and prior frames are not intermixed with the current view. Mode changes must not destroy recorded log output. Frame boundaries should be first-class data, rather than implemented by repeatedly resetting the scrollback store.

Proposed command: `display mode continuous|frame` (name provisional; verify against existing context-specific `display` commands before implementation). A future persisted profile option may select the startup mode. Neither mode depends on debug status.

## Architecture

1. **KIO** owns terminal input and converts keyboard, mouse, paste, and resize activity into structured events. Escape-sequence parsing must handle sequences split across reads.
2. **Command worker** owns command parsing, state transitions, scan execution, and project/target mutations. Its output functions publish immutable, owned text records. Commands never write directly to the terminal.
3. **Ordered output sink** writes logs independently of the screen and preserves output/frame ordering under bursts. Terminal paint scheduling must not cause lost logs.
4. **KUI event queue** transfers output, scroll, input snapshots, banner snapshots, resize, mode changes, and shutdown notifications. Specify bounded memory/backpressure and ownership for each payload.
5. **Renderer thread** alone writes ANSI terminal sequences, keeps current and desired screen buffers, coalesces events, and paints only changed cells/rows.
6. **Scrollback** holds logical lines plus frame IDs and maintains a width-specific wrapped-row index. On resize invalidate/rebuild the index. Viewports use an anchor that survives incoming output while scrolled back.
7. **Banner** receives snapshots of displayable state rather than dereferencing mutable command-owned structures.

## Frame semantics to settle during implementation

- Treat frame start as a command/output boundary rather than an implicit terminal clear.
- Choose whether a newly submitted command immediately replaces the visible frame or waits for its first output, then keep this behavior deterministic.
- Keep log history independent of what is shown in frame mode; switching back to continuous should have a documented policy for revealing preceding frames.
- Define how a running command's streaming output and interactive PTY programs behave in either mode.

## Migration sequence

1. Establish exclusive terminal ownership; replace KIO's direct paint calls with viewport/input events. Keep current command execution synchronous during this step.
2. Implement desired/current virtual-screen buffers, dirty regions, batched ANSI writes, and cursor restoration. Do not emit full terminal reset (ESC c) on ordinary shrink/resizes.
3. Introduce indexed scrollback, separate viewport anchoring from logical-line count, and both display modes.
4. Add the dedicated renderer thread and command worker with explicit start/stop and queue drain ordering.
5. Integrate tools PTY handoff or terminal emulation deliberately; do not send arbitrary interactive PTY control bytes into ordinary scrollback.

Keep the existing KUI implementation available as a baseline until the replacement is validated on Linux and FreeBSD. Avoid changing network scanning and book behavior as part of the UI migration.

## Acceptance tests

- Idle UI produces zero terminal redraws.
- Editing a command does not repaint the banner or scrollback.
- Burst mouse events are coalesced, without lost key events.
- Scrolled view stays anchored during high-volume incoming scan output.
- Continuous and frame modes switch predictably without dropping recorded output.
- ANSI colors, UTF-8, wrapping, long lines, and resize behave in both modes.
- Logging remains complete even when rendering lags.
- Interactive PTY tools preserve correct terminal state, including after exit and failure.
- Benchmark bytes written, paint latency, wrap-index cost, CPU idle use, and queue depth with the configured maximum scrollback.
