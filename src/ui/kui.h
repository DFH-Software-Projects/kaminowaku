// Copyright 2026 Jamison A. Drapeau
// kui.h - KAMI UI (full TUI render engine)
#ifndef KUI_H
#define KUI_H
#include "data.h"
void kui_enter(_carry_forward * _prog_data);
void kui_exit(void);
void kui_render_page(void);
/* @@ Phase 1: KIO reports input; only KUI paints the prompt and viewport. */
void kui_input_begin(void);
void kui_input_update(const char *prompt, const char *input, unsigned int cursor);
void kui_input_end(void);
void kui_input_scroll(int rows);
void kui_input_bell(void);

void kui_add_line(const char *fmt, ...);
void kui_add_line_and_render(const char *fmt, ...);
void kui_add_line_and_render_guard(const char *fmt, ...);
void kui_set_churning(const char *fmt, ...);
void kui_clear_churning(void);
void kui_progress_begin(_carry_forward * _prog_data);
void kui_progress_advance(void);
void kui_progress_end(void);
void kui_processing_begin(void);
void kui_processing_end(void);
void kui_scrollback_reset(void);
void kui_scrollback_scroll_by(KUI_SCROLLBACK *sb, int delta);
void kui_clear_output(void);
void kui_flush_log(void);
void kui_frame_start(void);
#if defined(__KSPING__H)
#define kui_add_line_and_render kui_add_line_and_render_guard
#endif
#endif
