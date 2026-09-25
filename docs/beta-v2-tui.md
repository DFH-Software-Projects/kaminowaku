# Beta V2 — TUI architecture and display modes

Status: Phases 1–3 implemented on beta-v2; Phase 4's thread-safe queue and stress test have been added, but the dedicated renderer and asynchronous command worker are not activated. Full-application integration verification and FreeBSD remain outstanding.

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

## Phase 1 implementation notes (2026-09-24)

- Added `src/ui/ui_events.h` and `src/ui/ui_events.c`: a fixed-capacity, ordered event ring. Each event owns its text snapshot. On queue saturation the synchronous KUI dispatcher drains before retrying.
- KIO retains raw terminal input, history, mouse parsing and editing decisions, but publishes scroll requests and prompt snapshots through new KUI APIs. It no longer paints the prompt or calls the page renderer directly.
- KUI now owns the existing prompt drawing algorithm (including ANSI-aware prompt width and horizontal input scrolling), mouse scroll presentation and terminal bell. The page-render function drains queued command output before painting, and re-anchors the input line after page updates.
- `kui_add_line()` and `kui_add_line_and_render()` retain their public signatures and formatting conventions. They post owned output events, which KUI drains in order into the existing log sink and scrollback. `kui_frame_start()`, `kui_flush_log()`, and shutdown drain preceding output to preserve logging order.
- The existing `RENDER_NORMAL` and `RENDER_SCROLLING` behavior is deliberately unchanged in this phase. The user-facing `continuous|frame` command belongs to Phase 3.
- This event ring is explicitly **not thread-safe** yet. Its sole consumer and producers run synchronously in Phase 1. Before Phase 4, introduce queue synchronization and a bounded backpressure policy for independent producers.
- Existing banner, progress, and external PTY pathways have not been migrated to exclusive terminal ownership. Complete compositor ownership and PTY integration in later phases without altering the command subsystem's notice syntax.

### Testing and cleanup

Keep targeted Phase 1 Linux queue and input tests during development. Consolidate or remove temporary harnesses and generated files by Phase 5; retain durable regression coverage and any tests needed for release validation. FreeBSD validation remains a separate platform gate.

### Phase 1 test inventory

- `tests/tui_phase1_events.c` validates FIFO event order, wraparound, saturation rejection, immutable producer snapshots and queue reset.
- `tests/test-tui-events.sh` builds the event test against the actual source tree without changing the primary Makefile. Run with `sh tests/test-tui-events.sh`.
- `.github/workflows/beta-v2-linux.yml` is branch-scoped and attempts the unit regression followed by a Linux `make OPENSSL_MODE=online` smoke build on each relevant push.
- Initial local Linux checks used a standalone queue harness compiled with AddressSanitizer and UndefinedBehaviorSanitizer, plus a prompt-painter compile/output harness. The full application binary and the GitHub Actions result still require separate confirmation. These checks are not substitutes for Phase 5 cross-platform and PTY tests.

## Phase 2 implementation notes

- Added `src/ui/ui_screen.h` and `src/ui/ui_screen.c`. The virtual screen retains current and desired physical rows, resets the desired content window for each frame, compares row text and incoming ANSI style, and paints only changed rows. Terminal writes are retry-safe for `EINTR` and partial writes.
- Reworked scrollback painting in `kui.c` to stage wrapped segments as rows instead of issuing separate writes for every glyph. The existing UTF-8-aware wrapping routine is retained; the renderer reconstructs active SGR sequences when a visible physical row starts in the middle of a styled logical line.
- Removed the banner's unconditional clear-to-end-of-screen operation. Its own header-row diff is still intact, and cursor visibility is now controlled by KUI.
- The input prompt uses the terminal's last physical row. KUI caches the last rendered input text and viewport; cursor-only movements reposition the cursor without repainting the line.
- Ordinary terminal shrinking no longer issues `ESC c` (terminal reset). Screen buffers and the banner invalidate on geometry changes; the small-terminal presentation clears only on entry to that state.
- A failure to stage a desired screen falls back to the legacy per-row writer for that frame.
- The original clear-on-command pathway remained during Phase 2. Phase 3 replaces it with persistent scrollback and logical frame slicing, without duplicating the compositor.

### Phase 2 verification

- `tests/tui_phase2_screen.c` tests identical-frame elimination, one-row repaint, stale-row clearing, width-change invalidation, ANSI styling and invalid row rejection.
- `tests/test-tui-screen.sh` compiles and runs the standalone screen regression. The Phase 2 test was also run locally on Linux with AddressSanitizer and UndefinedBehaviorSanitizer.
- `.github/workflows/beta-v2-linux.yml` includes both Phase 1 and Phase 2 standalone regressions and attempts a full Linux smoke build. The full application build and interactive terminal behavior have **not** been independently verified in this environment; FreeBSD validation remains deferred.
- Phase 5 will consolidate temporary harnesses and remove test products from the repository; retain durable UI regression coverage.

## Phase 3 implementation notes

- `src/ui/ui_wrap_index.h` and `src/ui/ui_wrap_index.c` maintain cached physical-row counts and prefix sums for the live 16,384-line scrollback buffer. Ordinary appends wrap only new lines; a single full-ring eviction shifts cached counts; width changes and multiple unseen evictions rebuild the index. A binary search maps a visible physical row to its logical line and wrapped segment.
- `kui.c` uses this index for page positioning. `view_offset` is consistently interpreted as a physical-row offset from the visible tail and clamped using the displayed viewport's actual wrapped-row count. While the user is scrolled back, appended output adjusts that offset by newly added minus evicted physical rows.
- The output store is retained across command boundaries. The renderer keeps an absolute output-sequence counter and records a frame-start sequence; **continuous** mode renders all retained scrollback, while **frame** mode renders only the active command's range. Switching modes does not delete stored scrollback or log entries.
- Default mode is **continuous** at TUI entry. The built-in `ui` command shows the current mode and `ui mode continuous|frame` changes it. The command is processed by the shared command parser in every command context. Debug flags no longer implicitly choose a display mode.
- `help` and `help ui` describe the new command. Existing notice styling and public `kui_add_line()` APIs are preserved.
- The fixed input row and incremental compositor are shared by both modes. Interactive PTY output remains a Phase 5 integration item.

### Phase 3 verification

- `tests/tui_phase3_wrap_index.c` covers append, resize, full-ring eviction and binary-search row lookup. The standalone index test passed locally on Linux with AddressSanitizer and UndefinedBehaviorSanitizer against the available compatible local scrollback definition; branch CI compiles against the current 16,384-line definition.
- `tests/test-tui-wrap-index.sh` and the beta-v2 Linux workflow include the index regression. Full application build, interactive mode switching, continuous scrollback under heavy scan output and native FreeBSD testing still require confirmation.
- Temporary tests and generated outputs are subject to the Phase 5 cleanup decision; keep permanent regressions where practical.

## Phase 4 foundation (not yet active)

- `src/ui/ui_events.c` now uses a POSIX mutex and condition variables. It preserves ordered, copied events and provides nonblocking posting, blocking backpressure for workers, waiting consumption, queue-draining close and a reset suitable for startup after all threads have joined.
- Existing KUI event consumption is **still synchronous**. Do not spawn a renderer thread until every terminal-writing pathway (including progress rendering and PTY handoff) is assigned an owner and the full build is verified. The current `kui_post_event()` full-queue fallback drains events in the calling thread; replace this with producer backpressure before activating concurrency.
- `tests/tui_phase4_queue.c` and `tests/test-tui-thread-queue.sh` stress 30,000 FIFO events with one producer and one consumer, bounded backpressure, drain-after-close and queue reset. The equivalent standalone queue test passed locally on Linux under AddressSanitizer and UndefinedBehaviorSanitizer; branch CI includes the regression and full-build smoke attempt.
- Before enabling the worker threads: finish banner/input snapshot ownership, make shutdown drain logs before closing streams, ensure PTY tools and processes fork safely, and instrument event backlog and render latency. Validate under a real pseudo-terminal on Linux, then FreeBSD.

## Phase 5 cleanup inventory

- Review all `tests/tui_phase*.c` and `tests/test-tui-*.sh`; retain stable regressions in a consolidated suite and delete obsolete one-off harnesses.
- Remove generated objects, binaries and temporary staging outputs; preserve release documentation if it materially helps maintainers.
