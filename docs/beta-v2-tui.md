# Beta V2 — TUI architecture and display modes

Status: Phases 1–4 are implemented on beta-v2, with Phase 5 PTY throttling, retained regression tests and instrumentation added. The main command dispatcher remains intentionally synchronous. Full application integration, live scanner/PTY acceptance and FreeBSD validation are outstanding.

## Objectives

Decouple terminal input, command execution, and presentation. Keep the public `kui_add_line()` family available to command handlers, but make it publish output without directly painting the terminal. Give the UI renderer exclusive ownership of terminal writes and viewport state. Improve scroll responsiveness without rescanning and repainting the entire scrollback for every wheel event.

## User-selectable display modes

The renderer will support two views over the same ordered output stream:

- **Continuous** (default): commands and their results accumulate in scrollback. The display follows the tail until the user navigates backward; incoming output must not dislodge a scrolled viewport.
- **Frame**: preserve the existing one-command/one-frame experience. The active command's frame is displayed, and prior frames are not intermixed with the current view. Mode changes must not destroy recorded log output. Frame boundaries should be first-class data, rather than implemented by repeatedly resetting the scrollback store.

Implemented command: `ui` reports the mode and `ui mode continuous|frame` changes it from any command context. Startup defaults to continuous. Display mode is independent of debug status and does not erase recorded output.

## Architecture

1. **KIO** owns terminal input and converts keyboard, mouse, paste, and resize activity into structured events. Escape-sequence parsing must handle sequences split across reads.
2. **Synchronous command dispatcher** retains the existing state machine, parsing, scanner scheduling, project/target mutations and execution on the main application thread. Its output functions publish owned UI records. We deliberately did not add a general-purpose command worker; scanner-level concurrency is outside the UI refactor and requires its own verification.
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
4. Add the dedicated renderer thread with explicit start/stop, ordered event delivery, synchronous state barriers and queue drain. Keep the command dispatcher synchronous.
5. Preserve the existing tools PTY parser and raw artifact logging; join/restart KUI across fork, throttle visual refresh only, and validate terminal restore and resize. Do not pass arbitrary PTY control bytes into normal scrollback.

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
- Historical Phase 1 limitation: the original ring was synchronous and not thread-safe. Phase 4 replaced its internals with a mutex/condition-variable queue and bounded producer backpressure.
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

## Phase 4 foundation (superseded by activation below)

- `src/ui/ui_events.c` now uses a POSIX mutex and condition variables. It preserves ordered, copied events and provides nonblocking posting, blocking backpressure for workers, waiting consumption, queue-draining close and a reset suitable for startup after all threads have joined.
- The dedicated renderer consumes queued UI events after TUI entry. Public render requests, input snapshots, output logging, state changes and log flushes are serialized through a producer/consumer acknowledgment barrier. A synchronous fallback remains if thread creation fails.
- `tests/tui_phase4_queue.c` and `tests/test-tui-thread-queue.sh` stress 30,000 FIFO events with one producer and one consumer, bounded backpressure, drain-after-close and queue reset. The equivalent standalone queue test passed locally on Linux under AddressSanitizer and UndefinedBehaviorSanitizer; branch CI includes the regression and full-build smoke attempt.
- Remaining work: input during long-running commands, full state-snapshot isolation from application mutations, metric collection and real PTY integration testing. Validate on a Linux pseudo-terminal, then FreeBSD before release.

## Phase 5 cleanup inventory

- Review all `tests/tui_phase*.c` and `tests/test-tui-*.sh`; retain stable regressions in a consolidated suite and delete obsolete one-off harnesses.
- Remove generated objects, binaries and temporary staging outputs; preserve release documentation if it materially helps maintainers.

## Phase 4 renderer activation

- On successful `kui_enter()`, KUI launches a dedicated pthread. It alone consumes the event queue and paints terminal output during ordinary interactive operation. KIO publishes input and scroll events; command handlers use the established `kui_add_line()` interfaces. KUI retains the legacy synchronous path if thread creation fails.
- Calls that must precede application state mutations—output logging, explicit page paints, frame starts, mode changes, churn indicator updates and runtime log flushes—use per-request condition-variable acknowledgments. Output production is bounded and ordered by the existing queue.
- The renderer reads mutable banner state only during an explicit synchronous paint, when the calling application thread is blocked on the acknowledgment. Asynchronous scroll events occur in KIO's ordinary input loop, where command execution is not simultaneously active. Network subsystems with their own mutation threads remain a separate integration-verification concern.
- `kui_guard.c` delegates mouse reporting to KUI instead of writing terminal escape codes independently.
- `tool_pty.c` calls `kui_fork_prepare()` to drain and join the renderer before `fork()`, then `kui_fork_parent()` to restart it only in the parent. This prevents inheriting a live KUI pthread into the external-tool child. The shutdown path joins the renderer before freeing screen buffers, resetting mouse modes, restoring the terminal and closing the runtime log.
- The application thread continues executing `cmd_scan()` synchronously by design; the separate command worker was explicitly removed from scope. Accept that keyboard commands and manual scroll input may wait until a synchronous scan completes.
- `tests/tui_phase4_handoff.c` covers an ordered 1,000-event drain, renderer join, fork and renderer restart. The isolated event-transport test passed locally with ASan/UBSan; the full KUI/PTTY runtime remains unverified.
- The branch CI workflow now runs the Phase 1–4 standalone regressions followed by a Linux `make OPENSSL_MODE=online` smoke attempt. A successful CI run has not been independently confirmed here.

### Renderer shutdown ordering

A render stop is enqueued by `ui_events_post_and_close()`: it atomically appends the final barrier and closes the producer side under the queue mutex. The renderer drains all preceding accepted events, acknowledges the stop, and is joined before the UI screen and runtime log are released. Late producers receive an error rather than posting behind STOP. On unexpectedly failed barrier initialization, the queue is closed and the renderer still joined.

The standalone Linux handoff regression exercises the final barrier, late-producer rejection, drain/join, `fork()`, and restart under AddressSanitizer/UndefinedBehaviorSanitizer. This verifies event-transport lifecycle behavior but **not** a full running Kaminowaku/PTTY session. The PTY fork integration and FreeBSD toolchain must still pass their dedicated acceptance tests.

## Phase 5 — PTY performance, diagnostics and test cleanup

### Implemented

- `tool_pty_stream_buffer()` commits every parsed output line normally and keeps the raw PTY `.out` artifact byte-for-byte independent of the display. Only `tool_pty_stream_status()` and `kui_render_page()` are rate-limited to approximately 30 visual updates per second, using `CLOCK_MONOTONIC`. The final status indicator is cleared with one last render, including when the final output burst is below the refresh interval.
- Removed redundant `kui_processing_begin()` calls during PTY stream bursts. Renderer pre-fork join and parent restart remain explicit; the child does not inherit an active renderer.
- Scroll events already at a viewport boundary no longer trigger a redundant page render. Output remains visible even if the optional runtime log stream is unavailable; when it exists, the log sink receives output before it is added to scrollback.
- The event queue tracks a resettable high-water mark through `ui_events_high_watermark()`. The virtual screen counts cumulative emitted row writes and bytes to support targeted performance diagnostics.
- Consolidated permanent regression entry point: `sh tests/run-tui-regressions.sh`; add `--bench` for compositor and maximum-scrollback measurements. Individual test scripts remain available, but they compile temporary binaries under `TMPDIR` with exit traps; transient local build artifacts are ignored. The branch CI now invokes the consolidated runner and builds the Linux application.
- `tests/tui_phase5_pty.c` independently checks OS PTY fork/exec, resize, raw ANSI capture, byte-perfect artifact replay and termios restoration. `tests/tui_phase5_perf.c` measures 1,000 unchanged frames, 1,000 changed-row frames, 1,000 cached lookups over 16,384 logical lines and 1,000 ring evictions.

### Linux test evidence and remaining release gates

The isolated Linux PTY contract check passed under AddressSanitizer and UndefinedBehaviorSanitizer. A local compositor/index benchmark at 16,384 lines recorded zero dirty rows across 1,000 unchanged frames and exactly 1,000 dirty rows across 1,000 single-row changes; timings are diagnostic and machine-dependent. These checks exercise the underlying modules and PTY primitives, **not the complete integrated application**.

Still requiring verification before merging `beta-v2` into `main`:

1. Confirm the current branch's complete Linux build and run an interactive PTY integration session, including repeated external tools, abnormal exits, resize and terminal restoration.
2. Exercise real built-in project and target scans and both display modes under sustained output; audit shared scanner counters read by the banner while scanner workers update them.
3. Validate the FreeBSD build and run the same interactive acceptance suite on an actual FreeBSD terminal.
4. Inspect GitHub Actions results and resolve any compiler or sanitizer findings. Retain the regression suite; remove any manually generated objects, binaries or staging files before release.
5. Keep this design document until the Beta V2 release review. Its long-term retention in `docs/` is a separate documentation decision.
