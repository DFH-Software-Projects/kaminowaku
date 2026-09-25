// Copyright 2026 Jamison A. Drapeau
// kui.c - KAMI UI render engine (full TUI skeleton)
// This file owns the "one command = one frame" behavior.
// Most of this is AI generated code with user directed modification.

/*

Little note about this code:

        - I should have been more hands on in writing this because it blew up fast
        - The AI really struggled here to do the heavy lift, don't blame it
        - Don't ever modify or add to this file, it just works.
*/

#define _XOPEN_SOURCE 700

#include "kui.h"
#include "ui_events.h"
#include "ui_screen.h"
#include "ui_wrap_index.h"
#include "banner.h"
#include "helpers.h"

#include <wchar.h>
#include <stdio.h>
#include <errno.h>
#include <locale.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <pthread.h>

/* ------------------------------------------------------------------------ */
/* Globals and macros                                                       */
/* ------------------------------------------------------------------------ */

#ifndef ANSI_CLEAR_SCREEN
#define ANSI_CLEAR_SCREEN   "\x1b[2J"
#endif

#ifndef ANSI_CURSOR_HOME
#define ANSI_CURSOR_HOME    "\x1b[H"
#endif

#define ANSI_ALT_SCREEN_ON  "\x1b[?1049h"
#define ANSI_ALT_SCREEN_OFF "\x1b[?1049l"
#define ANSI_CURSOR_HIDE    "\x1b[?25l"
#define ANSI_CURSOR_SHOW    "\x1b[?25h"

static _carry_forward *g_prog_data = NULL;
static char KUI_CHURNING[KUI_MAX_SCROLL_COLS];
static int8_t KUI_CHURNING_ACTIVE = ISFALSE;
static UI_SCREEN KUI_SCREEN;
static int KUI_SCREEN_READY = ISFALSE;
static int KUI_SCREEN_ACTIVE = ISFALSE;
static int KUI_SCREEN_FAILED = ISFALSE;
static UI_WRAP_INDEX KUI_WRAP_INDEX;
static unsigned int KUI_VIEW_MAX_OFF = 0;
static kui_display_mode_t KUI_DISPLAY_MODE = KUI_DISPLAY_CONTINUOUS;
static uint64_t KUI_OUTPUT_SEQUENCE = 0;
static uint64_t KUI_FRAME_START_SEQUENCE = 0;

// @@ Only the renderer thread paints once it has started.
static pthread_t KUI_RENDER_THREAD;
static int KUI_RENDER_RUNNING = ISFALSE;
static int KUI_MOUSE_ENABLED = ISFALSE;
static int KUI_SMALL = ISFALSE;
static unsigned int KUI_LAYOUT_TOP = 0;
static unsigned int KUI_LAYOUT_BOTTOM = 0;
static _Thread_local int KUI_RENDER_CONTEXT = ISFALSE;

typedef struct {
        pthread_mutex_t lock;
        pthread_cond_t ready;
        int done;
        int result;
} KUI_EVENT_ACK;

static void kui_render_page_owned(void);
static void kui_handle_event(const ui_event_t *event);
static int kui_post_event_sync(ui_event_t *event);
static void kui_dispatch_events(void);

/* ------------------------------------------------------------------------ */
/* Low-level terminal helpers                                               */
/* ------------------------------------------------------------------------ */

static void kui_reset_scroll_region(void) {
	(void)write(STDOUT_FILENO, "\x1b[r", 3);
}

static void kui_goto(unsigned row, unsigned col) {
	char buf[64];
	int n;
	if (row == 0) row = 1;
	if (col == 0) col = 1;
	n = snprintf(buf, sizeof(buf), "\x1b[%u;%uH", row, col);
	if (n > 0 && (size_t)n < sizeof(buf)) (void)write(STDOUT_FILENO, buf, (size_t)n);
}

static void kui_get_winsize(unsigned *rows_out, unsigned *cols_out) {
	struct winsize ws;
	unsigned rows = 24, cols = 80;
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
		if (ws.ws_row > 0) rows = ws.ws_row;
		if (ws.ws_col > 0) cols = ws.ws_col;
	}
	if (rows_out) *rows_out = rows;
	if (cols_out) *cols_out = cols;
}

static void kui_mouse_enable(void) {
        if (KUI_MOUSE_ENABLED == ISTRUE) return;
        (void)write(STDOUT_FILENO, "\x1b[?1000h", 8);
        (void)write(STDOUT_FILENO, "\x1b[?1002h", 8);
        (void)write(STDOUT_FILENO, "\x1b[?1006h", 8);
        (void)write(STDOUT_FILENO, "\x1b[?2004h", 8);
        KUI_MOUSE_ENABLED = ISTRUE;
}

static void kui_mouse_disable(void) {
        if (KUI_MOUSE_ENABLED != ISTRUE) return;
        (void)write(STDOUT_FILENO, "\x1b[?2004l", 8);
        (void)write(STDOUT_FILENO, "\x1b[?1006l", 8);
        (void)write(STDOUT_FILENO, "\x1b[?1002l", 8);
        (void)write(STDOUT_FILENO, "\x1b[?1000l", 8);
        KUI_MOUSE_ENABLED = ISFALSE;
}

/* ------------------------------------------------------------------------ */
/* Scrollback internals + Helpers                                           */
/* ------------------------------------------------------------------------ */

static void kui_scrollback_init(KUI_SCROLLBACK *sb) {
        unsigned i;
        if (!sb) return;
        sb->line_count  = 0;
        sb->view_offset = 0;
        sb->head        = 0;
        for (i = 0; i < KUI_MAX_SCROLL_LINES; i++) sb->lines[i][0] = '\0';
}
static int kui_is_space_byte(unsigned char c) {
        return (c == ' ' || c == '\t');
}

static size_t kui_ansi_seq_len(const char *s, size_t i, size_t n) {
        size_t j;
        if (!s || i >= n) return 0;
        if ((unsigned char)s[i] != 0x1b) return 0;
        /* CSI: ESC '[' ... final(0x40-0x7E) */
        if (i + 1 < n && s[i + 1] == '[') {
                j = i + 2;
                while (j < n) {
                        unsigned char c = (unsigned char)s[j];
                        if (c >= 0x40 && c <= 0x7e) return (j - i + 1);
                        j++;
                }
                return (n - i);
        }

        /* Other ESC: best effort */
        if (i + 1 < n) return 2;
        return 1;
}

static void kui_next_glyph(const char *s, size_t i, size_t n, int *w_out, size_t *blen_out) {
        mbstate_t st;
        wchar_t wc;
        size_t r;
        int w;
        size_t esc_len;
        if (!w_out || !blen_out) return;
        *w_out = 0;
        *blen_out = 0;
        if (!s || i >= n) return;
        if ((unsigned char)s[i] == 0x1b) {
                esc_len = kui_ansi_seq_len(s, i, n);
                if (esc_len == 0) esc_len = 1;
                *w_out = 0;
                *blen_out = esc_len;
                return;
        }
        memset(&st, 0, sizeof(st));
        r = mbrtowc(&wc, s + i, n - i, &st);
        if (r == (size_t)-1 || r == (size_t)-2 || r == 0) {
                *w_out = 1;
                *blen_out = 1;
                return;
        }
        w = wcwidth(wc);
        if (w < 0) w = 1;
        *w_out = w;
        *blen_out = r;
}

/* Return byte index end for a wrapped segment starting at 'start', fitting in 'cols'. */
static size_t kui_wrap_find_end(const char *s, size_t n, size_t start, unsigned int cols) {
        size_t i;
        unsigned int used = 0;
        size_t last_space = (size_t)-1;
        size_t last_space_end = (size_t)-1;
        int gw;
        size_t glen;
        size_t esc_len;
        if (!s || start >= n || cols == 0) return start;
        /* Skip leading spaces/tabs for each segment */
        i = start;
        while (i < n && kui_is_space_byte((unsigned char)s[i])) i++;
        start = i;
        while (i < n) {
                if ((unsigned char)s[i] == 0x1b) {
                        esc_len = kui_ansi_seq_len(s, i, n);
                        if (esc_len == 0) esc_len = 1;
                        i += esc_len;
                        continue;
                }
                kui_next_glyph(s, i, n, &gw, &glen);
                if (glen == 0) break;
                if (used + (unsigned int)gw > cols) break;
                if (glen == 1 && kui_is_space_byte((unsigned char)s[i])) {
                        last_space = i;
                        last_space_end = i + 1;
                }
                used += (unsigned int)gw;
                i += glen;
                if (used == cols) break;
        }
        if (i <= start) {
                kui_next_glyph(s, start, n, &gw, &glen);
                if (glen == 0) glen = 1;
                return start + glen;
        }
        if (i >= n) return n;
        if (last_space != (size_t)-1 && last_space >= start) {
                size_t end = last_space;
                while (end > start && kui_is_space_byte((unsigned char)s[end - 1])) end--;
                if (end == start) {
                        if (last_space_end != (size_t)-1) return last_space_end;
                        return i;
                }
                return end;
        }
        return i;
}

/* Count how many physical wrapped rows a logical line consumes at width cols. */
static unsigned int kui_line_wrap_rows(const char *s, unsigned int cols) {
        size_t n;
        size_t pos;
        unsigned int rows;
        if (!s) s = "";
        if (cols == 0) return 1;
        n = strnlen(s, KUI_MAX_SCROLL_COLS);
        if (n == 0) return 1;
        pos = 0;
        rows = 0;
        while (pos < n) {
                size_t end = kui_wrap_find_end(s, n, pos, cols);
                if (end <= pos) end = pos + 1;
                rows++;
                pos = end;
                while (pos < n && kui_is_space_byte((unsigned char)s[pos]))
                        pos++;
        }
        return (rows == 0) ? 1 : rows;
}

/* Total physical rows in scrollback at width cols. */
static unsigned int kui_scrollback_total_rows(const KUI_SCROLLBACK *sb, unsigned int cols) {
        if (!sb || cols == 0) return 0;
        ui_wrap_index_sync(&KUI_WRAP_INDEX, sb, cols, kui_line_wrap_rows);
        return KUI_WRAP_INDEX.total_rows;
}

// @@ Preserve active ANSI colors when a physical row starts mid-line.
// Only SGR sequences affect glyph style; cursor/mouse control bytes are never cached.
static size_t kui_style_before(const char *line, size_t pos, char *style, size_t cap) {
        size_t used = 0;
        size_t i = 0;
        if (!line || !style || cap == 0) return 0;
        while (i < pos) {
                if ((unsigned char)line[i] != 0x1b || i + 1 >= pos || line[i + 1] != '[') {
                        i++;
                        continue;
                }
                size_t n = kui_ansi_seq_len(line, i, pos);
                if (n < 3 || i + n > pos) break;
                if (line[i + n - 1] == 'm') {
                        if (line[i + 2] == 'm' || line[i + 2] == '0'
                                && (line[i + 3] == 'm' || line[i + 3] == ';'))
                                used = 0;
                        if (used + n < cap) {
                                memcpy(style + used, line + i, n);
                                used += n;
                        }
                }
                i += n;
        }
        style[used] = '\0';
        return used;
}

// @@ Render rows into the desired screen. A fallback retains legacy painting
// when allocation cannot support unusually large terminal dimensions.
static void kui_paint_row(unsigned int row, const char *text, size_t length,
        const char *style, size_t style_length) {
        if (KUI_SCREEN_ACTIVE == ISTRUE) {
                if (ui_screen_set(&KUI_SCREEN, row, text, length, style, style_length) == 0)
                        return;
                KUI_SCREEN_FAILED = ISTRUE;
                return;
        }
        kui_goto(row, 1);
        (void)write(STDOUT_FILENO, "\x1b[0m", 4);
        if (style && style_length)
                (void)write(STDOUT_FILENO, style, style_length);
        if (text && length)
                (void)write(STDOUT_FILENO, text, length);
        (void)write(STDOUT_FILENO, "\x1b[0m\x1b[K", 7);
}

static unsigned int kui_scrollback_render(
        const KUI_SCROLLBACK *sb,
        unsigned int content_top,
        unsigned int content_bottom,
        unsigned int cols
) {
        unsigned int window_h;
        unsigned int total_lines;
        unsigned int total_rows;
        unsigned int offset_rows;
        unsigned int max_off_rows;
        unsigned int first_row_idx;
        unsigned int bottom_row_idx;
        unsigned int first_logical = 0;
        unsigned int row_base = 0;
        /* Starting point in terms of logical line + byte pos */
        unsigned int start_logical = 0;
        size_t       start_pos = 0;
        unsigned int i;
        if (!sb) return 0;
        if (content_bottom < content_top) return 0;
        window_h = content_bottom - content_top + 1;
        if (window_h == 0) return 0;
        total_lines = sb->line_count;
        if (total_lines == 0) {
                KUI_VIEW_MAX_OFF = 0;
                // @@ Desired rows are already blank in the virtual screen.
                if (KUI_SCREEN_ACTIVE != ISTRUE)
                        for (i = 0; i < window_h; i++)
                                kui_paint_row(content_top + i, "", 0, "", 0);
                return 0;
        }
        // @@ Frame mode shows only output from the active command boundary.
        (void)kui_scrollback_total_rows(sb, cols);
        if (KUI_DISPLAY_MODE == KUI_DISPLAY_FRAME) {
                uint64_t oldest = KUI_OUTPUT_SEQUENCE >= total_lines
                        ? KUI_OUTPUT_SEQUENCE - total_lines : 0;
                if (KUI_FRAME_START_SEQUENCE > oldest) {
                        uint64_t gap = KUI_FRAME_START_SEQUENCE - oldest;
                        first_logical = gap < total_lines
                                ? (unsigned int)gap : total_lines;
                }
        }
        row_base = KUI_WRAP_INDEX.prefix[first_logical];
        total_rows = KUI_WRAP_INDEX.total_rows - row_base;
        if (total_rows == 0) {
                KUI_VIEW_MAX_OFF = 0;
                if (KUI_SCREEN_ACTIVE != ISTRUE)
                        for (i = 0; i < window_h; i++)
                                kui_paint_row(content_top + i, "", 0, "", 0);
                return 0;
        }
        if (total_rows > window_h) max_off_rows = total_rows - window_h;
        else max_off_rows = 0;
        KUI_VIEW_MAX_OFF = max_off_rows;
        offset_rows = sb->view_offset;
        if (offset_rows > max_off_rows) offset_rows = max_off_rows;
        /* Interpret view_offset as physical rows from bottom */
        bottom_row_idx = (total_rows == 0) ? 0 : (total_rows - 1);
        if (offset_rows <= bottom_row_idx) bottom_row_idx -= offset_rows;
        else bottom_row_idx = 0;
        if (bottom_row_idx + 1 > window_h) first_row_idx = bottom_row_idx + 1 - window_h;
        else first_row_idx = 0;
        // @@ Binary-search the cached prefix instead of re-wrapping old lines.
        {
                unsigned int inside = 0;
                if (ui_wrap_index_locate(&KUI_WRAP_INDEX, row_base + first_row_idx,
                        &start_logical, &inside) == 0) {
                        unsigned int physical = (sb->head + start_logical) % KUI_MAX_SCROLL_LINES;
                        const char *line = sb->lines[physical];
                        size_t n = strnlen(line, KUI_MAX_SCROLL_COLS);
                        size_t pos = 0;
                        for (unsigned int segment = 0; pos < n && segment < inside; segment++) {
                                size_t end = kui_wrap_find_end(line, n, pos, cols);
                                if (end <= pos) end = pos + 1;
                                pos = end;
                                while (pos < n && kui_is_space_byte((unsigned char)line[pos])) pos++;
                        }
                        start_pos = pos;
                }
        }
        /* Render forward from (start_logical, start_pos) until window_h rows filled */
        {
                unsigned int row_used = 0;
                unsigned int logical = start_logical;
                size_t pos = start_pos;
                while (logical < total_lines && row_used < window_h) {
                        unsigned int physical = (sb->head + logical) % KUI_MAX_SCROLL_LINES;
                        const char *s = sb->lines[physical];
                        size_t n;
                        if (!s) s = "";
                        n = strnlen(s, KUI_MAX_SCROLL_COLS);
                        /* Empty line consumes one physical row */
                        if (n == 0) {
                                kui_paint_row(content_top + row_used, "", 0, "", 0);
                                row_used++;
                                logical++;
                                pos = 0;
                                continue;
                        }
                        while (pos < n && row_used < window_h) {
                                size_t end = kui_wrap_find_end(s, n, pos, cols);
                                char style[UI_SCREEN_MAX_STYLE_BYTES];
                                size_t style_length;
                                if (end <= pos) end = pos + 1;
                                // @@ The wrapper already guarantees glyph boundaries.
                                // Carry ANSI SGR styling across physical row wraps.
                                style_length = kui_style_before(s, pos, style, sizeof(style));
                                kui_paint_row(content_top + row_used, s + pos,
                                        end - pos, style, style_length);
                                row_used++;
                                pos = end;
                                while (pos < n && kui_is_space_byte((unsigned char)s[pos])) pos++;
                        }
                        /* next logical line */
                        logical++;
                        pos = 0;
                }
                /* Clear remainder of content window */
                while (row_used < window_h) {
                        kui_paint_row(content_top + row_used, "", 0, "", 0);
                        row_used++;
                }
                /*
                 * Prompt anchoring:
                 * If total visible physical rows (total_rows - offset_rows) is less than window,
                 * return that so prompt sits directly under the last output row.
                 */
                {
                        unsigned int visible_rows = total_rows;
                        if (visible_rows > offset_rows) visible_rows -= offset_rows;
                        else visible_rows = 0;
                        if (visible_rows < window_h) return visible_rows;
                        return window_h;
                }
        }
}

static void kui_scrollback_push(const char *line) {
        KUI_SCROLLBACK *sb;
        unsigned int idx;
        unsigned int cols;
        unsigned int added_rows;
        unsigned int evicted_rows = 0;
        char tmp[KUI_MAX_SCROLL_COLS];
        size_t len;
        if (!g_prog_data || !line) return;
        sb = &g_prog_data->kui_scrollback;
        cols = g_prog_data->term_cols > 0 ? g_prog_data->term_cols : 80;
        len = strnlen(line, KUI_MAX_SCROLL_COLS - 1);
        if (len > 0 && line[len - 1] == '\n') len--;
        memcpy(tmp, line, len);
        tmp[len] = '\0';
        added_rows = kui_line_wrap_rows(tmp, cols);
        if (sb->line_count < KUI_MAX_SCROLL_LINES) {
                idx = (sb->head + sb->line_count) % KUI_MAX_SCROLL_LINES;
                sb->line_count++;
        } else {
                idx = sb->head;
                evicted_rows = kui_line_wrap_rows(sb->lines[idx], cols);
                sb->head = (sb->head + 1) % KUI_MAX_SCROLL_LINES;
                // @@ A complete wrap could make head/count look unchanged
                // after many unseen evictions. Force one index rebuild.
                if (sb->head == 0)
                        ui_wrap_index_reset(&KUI_WRAP_INDEX);
        }

        memcpy(sb->lines[idx], tmp, len);
        sb->lines[idx][len] = '\0';
        KUI_OUTPUT_SEQUENCE++;

        // @@ When a full ring evicts the oldest row, its removal shifts the
        // index of the visible anchor equally. Only newly appended rows must
        // be added to the tail-relative offset, unless that anchor was evicted.
        if (sb->view_offset > 0) {
                unsigned int delta = added_rows;
                sb->view_offset = delta > UINT32_MAX - sb->view_offset
                        ? UINT32_MAX : sb->view_offset + delta;
        }
        (void)evicted_rows;
}

static void kui_log_frame_header_if_needed(FILE *fp) {
        if (!g_prog_data || !fp) return;
        if (g_prog_data->log_frame_start == ISTRUE) {
                fprintf(fp, "--[%s]--\n", timestamp());
                g_prog_data->log_frame_start = ISFALSE;
        }
}

static void kui_sink_line(const char *line) {
        FILE *fp;
        if (!g_prog_data || !line) return;
        fp = (FILE *)g_prog_data->log;
        // @@ Presentation must not disappear if runtime logging is unavailable.
        // Preserve log ordering when an active stream exists.
        if (fp) {
                kui_log_frame_header_if_needed(fp);
                fprintf(fp, "%s\n", line);
                if (g_prog_data->debug_flag == ISTRUE) fflush(fp);
        }
        kui_scrollback_push(line);
}


// @@ Input display is KUI-owned. KIO only submits immutable editing snapshots.
#ifndef TAB_STOP
#define TAB_STOP 8
#endif

static int KUI_INPUT_ACTIVE = ISFALSE;
static int KUI_PUMPING = ISFALSE;
static char KUI_INPUT_TEXT[INPUT_BLOCK];
static char KUI_INPUT_PROMPT[DOUBLE_BLOCK];
static unsigned int KUI_INPUT_CURSOR = 0;
static int KUI_INPUT_DIRTY = ISTRUE;
static char KUI_INPUT_LAST_TEXT[INPUT_BLOCK];
static char KUI_INPUT_LAST_PROMPT[DOUBLE_BLOCK];
static int KUI_INPUT_LAST_WIDTH = 0;
static int KUI_INPUT_LAST_START = -1;

static int visible_width_noansi(const char *s)
{
        int w = 0;
        const unsigned char *p = (const unsigned char *)s;

        while (*p) {
                if (*p == '\x1b') {
                        // Skip CSI: ESC '[' ... final byte 0x40–0x7E
                        p++;
                        if (*p == '[') {
                                p++;
                                while (*p && !(*p >= 0x40 && *p <= 0x7E)) p++;
                                if (*p) p++;      // consume final
                        }
                        continue;
                }
                if (*p == '\t') {
                        int to_next = TAB_STOP - (w % TAB_STOP);
                        if (to_next < 0) to_next = 0;
                        w += to_next;
                        p++;
                        continue;
                }
                if (*p == '\r' || *p == '\n') {
                        // Keep it simple: treat as column reset on the same logical line.
                        // (If you *do* embed newlines in prompts, consider tracking rows too.)
                        w = 0;
                        p++;
                        continue;
                }
                w++;
                p++;
        }
        return w;
}

static void kui_input_draw(const char *prompt, const char *INPUT_BUFFER, int cursor) {
        struct winsize ws;
        ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws);
        int term_width = ws.ws_col > 0 ? ws.ws_col : 80;

        int prompt_vis = visible_width_noansi(prompt);
        int available  = term_width - prompt_vis;   // space left for input text

        if (available < 1)
                available = 1;                      // always leave *some* room

        int len = (int)strlen(INPUT_BUFFER);
        if (cursor < 0)
                cursor = 0;
        if (cursor > len)
                cursor = len;

        /* Decide which slice of INPUT_BUFFER to show so that the cursor stays visible. */
        int start = 0;
        if (cursor >= available) {
                start = cursor - (available - 1);
                if (start < 0)
                        start = 0;
        }

        int view_len = len - start;
        if (view_len > available)
                view_len = available;

        // @@ A cursor-only edit never repaints the input text.
        int display_cursor = cursor - start;
        int target_col = prompt_vis + display_cursor;
        if (KUI_INPUT_DIRTY != ISTRUE && KUI_INPUT_LAST_WIDTH == term_width
                && KUI_INPUT_LAST_START == start
                && strcmp(KUI_INPUT_LAST_PROMPT, prompt) == 0
                && strcmp(KUI_INPUT_LAST_TEXT, INPUT_BUFFER) == 0) {
                printf("\033[u\r");
                if (target_col > 0) printf("\033[%dC", target_col);
                fflush(stdout);
                return;
        }

        /* Restore anchor, clear the line, and draw prompt + visible slice. */
        printf("\033[u");          /* restore saved cursor (anchor at line start) */
        printf("\r\033[K");        /* CR + clear to end of line */
        printf("%s", prompt);
        if (view_len > 0)
                fwrite(INPUT_BUFFER + start, 1, (size_t)view_len, stdout);

        /* Move cursor to the correct spot within the visible slice. */
        /* Position already computed before checking the cached input. */

        /* We are currently at column (prompt_vis + view_len); move left if needed. */
        int current_col = prompt_vis + view_len;
        int move_left   = current_col - target_col;

        if (move_left > 0)
                printf("\033[%dD", move_left);

        fflush(stdout);
        snprintf(KUI_INPUT_LAST_PROMPT, sizeof(KUI_INPUT_LAST_PROMPT), "%s", prompt);
        snprintf(KUI_INPUT_LAST_TEXT, sizeof(KUI_INPUT_LAST_TEXT), "%s", INPUT_BUFFER);
        KUI_INPUT_LAST_WIDTH = term_width;
        KUI_INPUT_LAST_START = start;
        KUI_INPUT_DIRTY = ISFALSE;
}

static void kui_input_anchor(void) {
        (void)write(STDOUT_FILENO, "\x1b[s", 3);
        fflush(stdout);
}

// @@ Every terminal-writing operation is serialized by the renderer.
// The existing one-thread fallback remains available if pthread_create fails.
static int kui_is_renderer(void) {
        return KUI_RENDER_CONTEXT == ISTRUE;
}

static void kui_ack_event(KUI_EVENT_ACK *ack, int result) {
        if (!ack) return;
        pthread_mutex_lock(&ack->lock);
        ack->result = result;
        ack->done = ISTRUE;
        pthread_cond_signal(&ack->ready);
        pthread_mutex_unlock(&ack->lock);
}

static void kui_handle_event(const ui_event_t *event) {
        int result = 0;
        if (!event) return;
        switch (event->type) {
                case UI_EVENT_OUTPUT:
                        kui_sink_line(event->output);
                        break;
                case UI_EVENT_SCROLL:
                        if (g_prog_data) {
                                unsigned int previous = g_prog_data->kui_scrollback.view_offset;
                                kui_scrollback_scroll_by(&g_prog_data->kui_scrollback,
                                        event->scroll_rows);
                                // @@ Don't repaint when the user is already at the
                                // top/bottom boundary or a wheel event is neutral.
                                if (previous != g_prog_data->kui_scrollback.view_offset)
                                        kui_render_page_owned();
                        }
                        break;
                case UI_EVENT_INPUT:
                        snprintf(KUI_INPUT_TEXT, sizeof(KUI_INPUT_TEXT), "%s", event->input);
                        snprintf(KUI_INPUT_PROMPT, sizeof(KUI_INPUT_PROMPT), "%s", event->prompt);
                        KUI_INPUT_CURSOR = event->cursor;
                        if (KUI_INPUT_ACTIVE == ISTRUE && KUI_SMALL != ISTRUE)
                                kui_input_draw(KUI_INPUT_PROMPT, KUI_INPUT_TEXT,
                                        (int)KUI_INPUT_CURSOR);
                        break;
                case UI_EVENT_RENDER:
                        kui_render_page_owned();
                        break;
                case UI_EVENT_INPUT_BEGIN:
                        KUI_INPUT_ACTIVE = ISTRUE;
                        KUI_INPUT_DIRTY = ISTRUE;
                        if (KUI_SMALL != ISTRUE) kui_input_anchor();
                        break;
                case UI_EVENT_INPUT_END:
                        KUI_INPUT_ACTIVE = ISFALSE;
                        KUI_INPUT_DIRTY = ISTRUE;
                        break;
                case UI_EVENT_BELL:
                        (void)write(STDOUT_FILENO, "\a", 1);
                        break;
                case UI_EVENT_FRAME_START:
                        if (g_prog_data) {
                                g_prog_data->log_frame_start = ISTRUE;
                                KUI_FRAME_START_SEQUENCE = KUI_OUTPUT_SEQUENCE;
                                g_prog_data->kui_scrollback.view_offset = 0;
                        }
                        break;
                case UI_EVENT_CLEAR:
                        if (g_prog_data) {
                                kui_scrollback_init(&g_prog_data->kui_scrollback);
                                ui_wrap_index_reset(&KUI_WRAP_INDEX);
                                KUI_VIEW_MAX_OFF = 0;
                                KUI_OUTPUT_SEQUENCE = 0;
                                KUI_FRAME_START_SEQUENCE = 0;
                                ui_screen_invalidate(&KUI_SCREEN);
                        }
                        break;
                case UI_EVENT_FLUSH:
                        if (g_prog_data && g_prog_data->log)
                                fflush((FILE *)g_prog_data->log);
                        break;
                case UI_EVENT_CHURNING:
                        snprintf(KUI_CHURNING, sizeof(KUI_CHURNING), "%s", event->output);
                        KUI_CHURNING_ACTIVE = ISTRUE;
                        break;
                case UI_EVENT_CHURNING_CLEAR:
                        memset(KUI_CHURNING, 0, sizeof(KUI_CHURNING));
                        KUI_CHURNING_ACTIVE = ISFALSE;
                        break;
                case UI_EVENT_MODE:
                        if (event->value != KUI_DISPLAY_CONTINUOUS
                                && event->value != KUI_DISPLAY_FRAME) {
                                result = -1;
                                break;
                        }
                        KUI_DISPLAY_MODE = (kui_display_mode_t)event->value;
                        KUI_VIEW_MAX_OFF = 0;
                        if (g_prog_data)
                                g_prog_data->kui_scrollback.view_offset = 0;
                        ui_screen_invalidate(&KUI_SCREEN);
                        break;
                case UI_EVENT_MODE_GET:
                        result = (int)KUI_DISPLAY_MODE;
                        break;
                case UI_EVENT_MOUSE:
                        if (event->value == ISTRUE) kui_mouse_enable();
                        else kui_mouse_disable();
                        break;
                case UI_EVENT_STOP:
                        break;
                default:
                        result = -1;
                        break;
        }
        kui_ack_event((KUI_EVENT_ACK *)event->completion, result);
}

static void kui_dispatch_events(void) {
        ui_event_t event;
        if (KUI_RENDER_RUNNING == ISTRUE) return;
        if (KUI_PUMPING == ISTRUE) return;
        KUI_PUMPING = ISTRUE;
        while (ui_events_next(&event) == 1)
                kui_handle_event(&event);
        KUI_PUMPING = ISFALSE;
}

// @@ Synchronous requests provide an ordering barrier for runtime logging,
// banner snapshots, state transitions and terminal handoff.
static int kui_post_event_sync(ui_event_t *event) {
        KUI_EVENT_ACK ack;
        int result;
        if (!event) return -1;
        if (KUI_RENDER_RUNNING != ISTRUE || kui_is_renderer()) {
                int direct_result = 0;
                event->completion = NULL;
                if (event->type == UI_EVENT_MODE_GET)
                        return (int)KUI_DISPLAY_MODE;
                if (event->type == UI_EVENT_MODE
                        && event->value != KUI_DISPLAY_CONTINUOUS
                        && event->value != KUI_DISPLAY_FRAME)
                        direct_result = -1;
                if (direct_result == 0) kui_handle_event(event);
                return direct_result;
        }
        if (pthread_mutex_init(&ack.lock, NULL) != 0) return -1;
        if (pthread_cond_init(&ack.ready, NULL) != 0) {
                pthread_mutex_destroy(&ack.lock);
                return -1;
        }
        ack.done = ISFALSE;
        ack.result = -1;
        event->completion = &ack;
        if (ui_events_post_wait(event) != 0) {
                pthread_cond_destroy(&ack.ready);
                pthread_mutex_destroy(&ack.lock);
                return -1;
        }
        pthread_mutex_lock(&ack.lock);
        while (ack.done != ISTRUE)
                pthread_cond_wait(&ack.ready, &ack.lock);
        result = ack.result;
        pthread_mutex_unlock(&ack.lock);
        pthread_cond_destroy(&ack.ready);
        pthread_mutex_destroy(&ack.lock);
        return result;
}

static void kui_post_event(const ui_event_t *event) {
        ui_event_t copy;
        if (!event) return;
        copy = *event;
        copy.completion = NULL;
        if (KUI_RENDER_RUNNING != ISTRUE) {
                if (ui_events_post(&copy) != 0) {
                        kui_dispatch_events();
                        if (ui_events_post(&copy) != 0)
                                kui_handle_event(&copy);
                }
                kui_dispatch_events();
                return;
        }
        (void)ui_events_post_wait(&copy);
}

static void *kui_render_thread_main(void *unused) {
        ui_event_t event;
        (void)unused;
        KUI_RENDER_CONTEXT = ISTRUE;
        while (ui_events_wait_next(&event) == 1) {
                int stop = event.type == UI_EVENT_STOP;
                kui_handle_event(&event);
                if (stop) break;
        }
        KUI_RENDER_CONTEXT = ISFALSE;
        return NULL;
}

static int kui_render_thread_start(void) {
        if (KUI_RENDER_RUNNING == ISTRUE) return 0;
        ui_events_reset();
        KUI_RENDER_RUNNING = ISTRUE;
        if (pthread_create(&KUI_RENDER_THREAD, NULL,
                        kui_render_thread_main, NULL) != 0) {
                KUI_RENDER_RUNNING = ISFALSE;
                return -1;
        }
        return 0;
}

// @@ Joining before fork keeps pthread mutexes/stdio out of the child.
static void kui_render_thread_stop(void) {
        ui_event_t event = {0};
        KUI_EVENT_ACK ack;
        if (KUI_RENDER_RUNNING != ISTRUE) return;
        if (pthread_mutex_init(&ack.lock, NULL) != 0) {
                // @@ Even allocation failures must join before screen teardown.
                ui_events_close();
                (void)pthread_join(KUI_RENDER_THREAD, NULL);
                KUI_RENDER_RUNNING = ISFALSE;
                ui_events_reset();
                return;
        }
        if (pthread_cond_init(&ack.ready, NULL) != 0) {
                pthread_mutex_destroy(&ack.lock);
                ui_events_close();
                (void)pthread_join(KUI_RENDER_THREAD, NULL);
                KUI_RENDER_RUNNING = ISFALSE;
                ui_events_reset();
                return;
        }
        ack.done = ISFALSE;
        ack.result = -1;
        event.type = UI_EVENT_STOP;
        event.completion = &ack;

        // @@ Atomic close prevents another producer queuing work after STOP.
        if (ui_events_post_and_close(&event) == 0) {
                pthread_mutex_lock(&ack.lock);
                while (ack.done != ISTRUE)
                        pthread_cond_wait(&ack.ready, &ack.lock);
                pthread_mutex_unlock(&ack.lock);
        }
        (void)pthread_join(KUI_RENDER_THREAD, NULL);
        KUI_RENDER_RUNNING = ISFALSE;
        pthread_cond_destroy(&ack.ready);
        pthread_mutex_destroy(&ack.lock);
        ui_events_reset();
}

void kui_fork_prepare(void) {
        kui_render_thread_stop();
}

void kui_fork_parent(void) {
        if (g_prog_data) (void)kui_render_thread_start();
}

void kui_input_begin(void) {
        ui_event_t event = {0};
        event.type = UI_EVENT_INPUT_BEGIN;
        (void)kui_post_event_sync(&event);
}

void kui_input_update(const char *prompt, const char *input, unsigned int cursor) {
        ui_event_t event = {0};
        event.type = UI_EVENT_INPUT;
        if (prompt) snprintf(event.prompt, sizeof(event.prompt), "%s", prompt);
        if (input) snprintf(event.input, sizeof(event.input), "%s", input);
        event.cursor = cursor;
        (void)kui_post_event_sync(&event);
}

void kui_input_end(void) {
        ui_event_t event = {0};
        event.type = UI_EVENT_INPUT_END;
        (void)kui_post_event_sync(&event);
}

void kui_input_scroll(int rows) {
        ui_event_t event = {0};
        event.type = UI_EVENT_SCROLL;
        event.scroll_rows = rows;
        kui_post_event(&event);
}

void kui_input_refresh_page(void) {
        kui_render_page();
}

void kui_input_bell(void) {
        ui_event_t event = {0};
        event.type = UI_EVENT_BELL;
        (void)kui_post_event_sync(&event);
}

void kui_mouse_capture_set(int enabled) {
        ui_event_t event = {0};
        event.type = UI_EVENT_MOUSE;
        event.value = enabled ? ISTRUE : ISFALSE;
        (void)kui_post_event_sync(&event);
}

/* ------------------------------------------------------------------------ */
/* Rendering                                                                */
/* ------------------------------------------------------------------------ */

static void kui_render_small(unsigned rows, unsigned cols) {
        (void)cols;
        if (KUI_SMALL != ISTRUE) {
                (void)write(STDOUT_FILENO, ANSI_CURSOR_HIDE, strlen(ANSI_CURSOR_HIDE));
                (void)write(STDOUT_FILENO, ANSI_CLEAR_SCREEN ANSI_CURSOR_HOME,
                        sizeof(ANSI_CLEAR_SCREEN ANSI_CURSOR_HOME) - 1);
                (void)write(STDOUT_FILENO, "TUI disabled (window too small).\n",
                        sizeof("TUI disabled (window too small).\n") - 1);
                KUI_SMALL = ISTRUE;
        }
        kui_goto(rows, 1);
        (void)write(STDOUT_FILENO, ANSI_CURSOR_SHOW, strlen(ANSI_CURSOR_SHOW));
}

// @@ A resize changes the number of physical rows, not the text the user
// selected. Preserve the first visible logical line and its byte segment.
typedef struct {
        unsigned int logical;
        size_t byte;
        int valid;
} KUI_VIEW_ANCHOR;
static KUI_VIEW_ANCHOR KUI_SAVED_ANCHOR = {0};

static KUI_VIEW_ANCHOR kui_anchor_before_resize(const KUI_SCROLLBACK *sb,
        unsigned int old_cols) {
        KUI_VIEW_ANCHOR anchor = {0};
        unsigned int first = 0, inside = 0, height, total, off;
        if (!sb || !sb->line_count || !old_cols || !sb->view_offset ||
                KUI_LAYOUT_BOTTOM < KUI_LAYOUT_TOP || !KUI_LAYOUT_TOP)
                return anchor;
        total = kui_scrollback_total_rows(sb, old_cols);
        height = KUI_LAYOUT_BOTTOM - KUI_LAYOUT_TOP + 1;
        off = sb->view_offset < total ? sb->view_offset : total;
        if (total > height + off) first = total - height - off;
        if (ui_wrap_index_locate(&KUI_WRAP_INDEX, first, &anchor.logical, &inside))
                return anchor;
        const char *line = sb->lines[(sb->head + anchor.logical) % KUI_MAX_SCROLL_LINES];
        size_t n = strnlen(line, KUI_MAX_SCROLL_COLS), pos = 0;
        for (unsigned int segment = 0; pos < n && segment < inside; segment++) {
                size_t end = kui_wrap_find_end(line, n, pos, old_cols);
                pos = end > pos ? end : pos + 1;
                while (pos < n && kui_is_space_byte((unsigned char)line[pos])) pos++;
        }
        anchor.byte = pos;
        anchor.valid = ISTRUE;
        return anchor;
}

static void kui_anchor_after_resize(KUI_SCROLLBACK *sb, unsigned int cols,
        unsigned int height, KUI_VIEW_ANCHOR anchor) {
        unsigned int logical, base = 0, segment = 0;
        unsigned int total, row, first;
        if (!anchor.valid || !sb || !height || !cols || !sb->line_count) return;
        (void)kui_scrollback_total_rows(sb, cols);
        logical = anchor.logical < sb->line_count ? anchor.logical : sb->line_count - 1;
        if (KUI_DISPLAY_MODE == KUI_DISPLAY_FRAME) {
                uint64_t oldest = KUI_OUTPUT_SEQUENCE >= sb->line_count
                        ? KUI_OUTPUT_SEQUENCE - sb->line_count : 0;
                if (KUI_FRAME_START_SEQUENCE > oldest) {
                        uint64_t gap = KUI_FRAME_START_SEQUENCE - oldest;
                        unsigned int first_logical = gap < sb->line_count
                                ? (unsigned int)gap : sb->line_count;
                        base = KUI_WRAP_INDEX.prefix[first_logical];
                }
        }
        const char *line = sb->lines[(sb->head + logical) % KUI_MAX_SCROLL_LINES];
        size_t n = strnlen(line, KUI_MAX_SCROLL_COLS), pos = 0;
        while (pos < n && pos < anchor.byte) {
                size_t end = kui_wrap_find_end(line, n, pos, cols);
                if (end > anchor.byte) break;
                pos = end > pos ? end : pos + 1;
                while (pos < n && kui_is_space_byte((unsigned char)line[pos])) pos++;
                segment++;
        }
        row = KUI_WRAP_INDEX.prefix[logical] + segment;
        first = row > base ? row - base : 0;
        total = KUI_WRAP_INDEX.total_rows - base;
        sb->view_offset = total > first + height ? total - first - height : 0;
}

static void kui_render_page_owned(void) {
        unsigned rows, cols, old_rows, old_cols, content_top, content_bottom, prompt_row;
        int8_t first_time, size_changed;
        int banner_rows;
        KUI_SCROLLBACK *sb;
        KUI_VIEW_ANCHOR resize_anchor = {0};

        kui_dispatch_events();
        if (!g_prog_data) return;
        kui_get_winsize(&rows, &cols);
        old_rows = g_prog_data->term_rows;
        old_cols = g_prog_data->term_cols;
        first_time = (old_rows == 0 && old_cols == 0);
        size_changed = (first_time || old_rows != rows || old_cols != cols);
        if (size_changed && !first_time && KUI_SMALL != ISTRUE)
                resize_anchor = kui_anchor_before_resize(
                        &g_prog_data->kui_scrollback, old_cols);
        if (size_changed) {
                g_prog_data->term_rows = rows;
                g_prog_data->term_cols = cols;
                banner_reset();
                ui_screen_invalidate(&KUI_SCREEN);
                KUI_INPUT_DIRTY = ISTRUE;
                kui_reset_scroll_region();
        }
        // @@ Do not attempt to fit a multi-line banner into an unusably
        // narrow terminal, or overwrite the input row on a tiny terminal.
        if (rows < KUI_MIN_ROWS || cols < 48) {
                if (resize_anchor.valid) KUI_SAVED_ANCHOR = resize_anchor;
                KUI_LAYOUT_TOP = KUI_LAYOUT_BOTTOM = 0;
                if (size_changed) KUI_SMALL = ISFALSE;
                ui_screen_invalidate(&KUI_SCREEN);
                kui_render_small(rows, cols);
                return;
        }
        if (KUI_SMALL == ISTRUE) {
                if (KUI_SAVED_ANCHOR.valid) resize_anchor = KUI_SAVED_ANCHOR;
                KUI_SAVED_ANCHOR.valid = ISFALSE;
                KUI_SMALL = ISFALSE;
                banner_reset();
                ui_screen_invalidate(&KUI_SCREEN);
                KUI_INPUT_DIRTY = ISTRUE;
                (void)write(STDOUT_FILENO, ANSI_CLEAR_SCREEN ANSI_CURSOR_HOME,
                        sizeof(ANSI_CLEAR_SCREEN ANSI_CURSOR_HOME) - 1);
        }

        (void)write(STDOUT_FILENO, ANSI_CURSOR_HIDE, strlen(ANSI_CURSOR_HIDE));
        banner_rows = banner(g_prog_data);
        if (banner_rows < 0) banner_rows = 0;
        content_top = (unsigned)banner_rows + 1;
        // @@ The input owns the bottom row, independently of scrollback.
        prompt_row = rows;
        content_bottom = rows > (KUI_CHURNING_ACTIVE == ISTRUE ? 2U : 1U)
                ? rows - (KUI_CHURNING_ACTIVE == ISTRUE ? 2U : 1U)
                : rows;
        if (content_top > content_bottom) content_top = content_bottom;
        if ((unsigned int)banner_rows >= rows - 1) {
                if (resize_anchor.valid) KUI_SAVED_ANCHOR = resize_anchor;
                KUI_LAYOUT_TOP = KUI_LAYOUT_BOTTOM = 0;
                KUI_SMALL = ISFALSE;
                ui_screen_invalidate(&KUI_SCREEN);
                kui_render_small(rows, cols);
                return;
        }
        if (resize_anchor.valid)
                kui_anchor_after_resize(&g_prog_data->kui_scrollback, cols,
                        content_bottom - content_top + 1, resize_anchor);
        KUI_LAYOUT_TOP = content_top;
        KUI_LAYOUT_BOTTOM = content_bottom;

        KUI_SCREEN_ACTIVE = ISFALSE;
        KUI_SCREEN_FAILED = ISFALSE;
        if (KUI_SCREEN_READY == ISTRUE
                && ui_screen_begin(&KUI_SCREEN, rows, cols, content_top, rows - 1) == 0)
                KUI_SCREEN_ACTIVE = ISTRUE;

        sb = &g_prog_data->kui_scrollback;
        if (sb->line_count == 0 && sb->head == 0 && sb->view_offset == 0)
                kui_scrollback_init(sb);

        (void)kui_scrollback_render(sb, content_top, content_bottom, cols);
        if (sb->view_offset > KUI_VIEW_MAX_OFF)
                sb->view_offset = KUI_VIEW_MAX_OFF;
        if (KUI_CHURNING_ACTIVE == ISTRUE)
                kui_paint_row(rows - 1, KUI_CHURNING,
                        strnlen(KUI_CHURNING, sizeof(KUI_CHURNING)), "", 0);

        // @@ If a desired row could not be staged, paint this frame using the
        // legacy row writer rather than committing a partially staged screen.
        if (KUI_SCREEN_FAILED == ISTRUE) {
                KUI_SCREEN_ACTIVE = ISFALSE;
                ui_screen_invalidate(&KUI_SCREEN);
                (void)kui_scrollback_render(sb, content_top, content_bottom, cols);
                if (KUI_CHURNING_ACTIVE == ISTRUE)
                        kui_paint_row(rows - 1, KUI_CHURNING,
                                strnlen(KUI_CHURNING, sizeof(KUI_CHURNING)), "", 0);
        } else if (KUI_SCREEN_ACTIVE == ISTRUE) {
                if (ui_screen_commit(&KUI_SCREEN) != 0)
                        ui_screen_invalidate(&KUI_SCREEN);
        }

        KUI_SCREEN_ACTIVE = ISFALSE;
        kui_goto(prompt_row, 1);
        (void)write(STDOUT_FILENO, ANSI_CURSOR_SHOW, strlen(ANSI_CURSOR_SHOW));
        fflush(stdout);
        if (KUI_INPUT_ACTIVE == ISTRUE) {
                kui_input_anchor();
                kui_input_draw(KUI_INPUT_PROMPT, KUI_INPUT_TEXT, (int)KUI_INPUT_CURSOR);
        }
}

void kui_render_page(void) {
        ui_event_t event = {0};
        if (KUI_RENDER_RUNNING != ISTRUE || kui_is_renderer()) {
                kui_render_page_owned();
                return;
        }
        event.type = UI_EVENT_RENDER;
        (void)kui_post_event_sync(&event);
}

/* ------------------------------------------------------------------------ */
/* Scrollback/Output/Enter&Exit + public API                                */
/* ------------------------------------------------------------------------ */

void kui_enter(_carry_forward * _prog_data) {
        if (_prog_data && !g_prog_data) g_prog_data = _prog_data;
        ui_events_reset();
        ui_wrap_index_reset(&KUI_WRAP_INDEX);
        KUI_VIEW_MAX_OFF = 0;
        KUI_DISPLAY_MODE = KUI_DISPLAY_CONTINUOUS;
        KUI_OUTPUT_SEQUENCE = 0;
        KUI_FRAME_START_SEQUENCE = 0;
        KUI_INPUT_ACTIVE = ISFALSE;
        KUI_INPUT_DIRTY = ISTRUE;
        KUI_SMALL = ISFALSE;
        KUI_MOUSE_ENABLED = ISFALSE;
        KUI_LAYOUT_TOP = KUI_LAYOUT_BOTTOM = 0;
        KUI_SAVED_ANCHOR.valid = ISFALSE;
        if (ui_screen_init(&KUI_SCREEN, STDOUT_FILENO) == 0)
                KUI_SCREEN_READY = ISTRUE;
        memset(KUI_CHURNING, 0x00, sizeof(KUI_CHURNING));
        KUI_CHURNING_ACTIVE = ISFALSE;

        // @@ Initial alternate-screen transition precedes renderer thread ownership.
        kui_mouse_enable();
        (void)write(STDOUT_FILENO, ANSI_ALT_SCREEN_ON, strlen(ANSI_ALT_SCREEN_ON));
        (void)write(STDOUT_FILENO, ANSI_CURSOR_HIDE, strlen(ANSI_CURSOR_HIDE));
        (void)write(STDOUT_FILENO, ANSI_CLEAR_SCREEN ANSI_CURSOR_HOME,
                sizeof(ANSI_CLEAR_SCREEN ANSI_CURSOR_HOME) - 1);
        banner_reset();
        fflush(stdout);
        (void)kui_render_thread_start();
}

void kui_exit(void) {
        // @@ Drain all accepted output before closing logs or restoring the shell.
        kui_render_thread_stop();
        kui_dispatch_events();
        KUI_INPUT_ACTIVE = ISFALSE;
        KUI_SCREEN_ACTIVE = ISFALSE;
        ui_screen_destroy(&KUI_SCREEN);
        KUI_SCREEN_READY = ISFALSE;
        if (g_prog_data) {
                FILE *fp = (FILE *)g_prog_data->log;
                if (fp) fflush(fp);
                g_prog_data = NULL;
        }
        memset(KUI_CHURNING, 0x00, sizeof(KUI_CHURNING));
        KUI_CHURNING_ACTIVE = ISFALSE;
        kui_mouse_disable();
        kui_reset_scroll_region();
        (void)write(STDOUT_FILENO, ANSI_CLEAR_SCREEN ANSI_CURSOR_HOME,
                sizeof(ANSI_CLEAR_SCREEN ANSI_CURSOR_HOME) - 1);
        (void)write(STDOUT_FILENO, ANSI_CURSOR_SHOW, strlen(ANSI_CURSOR_SHOW));
        (void)write(STDOUT_FILENO, ANSI_ALT_SCREEN_OFF, strlen(ANSI_ALT_SCREEN_OFF));
        fflush(stdout);
        ui_events_close();
}

void kui_scrollback_reset(void) {
        ui_event_t event = {0};
        if (!g_prog_data) return;
        event.type = UI_EVENT_CLEAR;
        (void)kui_post_event_sync(&event);
}

static unsigned int kui_scrollback_max_off(const KUI_SCROLLBACK *sb) {
        if (!sb) return 0;
        return KUI_VIEW_MAX_OFF;
}

static void kui_scrollback_set_view_offset(KUI_SCROLLBACK *sb, unsigned int new_off) {
        unsigned int max_off;
        if (!sb) return;
        max_off = kui_scrollback_max_off(sb);
        if (new_off > max_off) new_off = max_off;
        sb->view_offset = new_off;
}

void kui_scrollback_scroll_by(KUI_SCROLLBACK *sb, int delta) {
        unsigned int max_off;
        int next;
        if (!sb) return;
        if (KUI_RENDER_RUNNING == ISTRUE && !kui_is_renderer()) {
                ui_event_t event = {0};
                if (g_prog_data && sb == &g_prog_data->kui_scrollback) {
                        event.type = UI_EVENT_SCROLL;
                        event.scroll_rows = delta;
                        (void)kui_post_event_sync(&event);
                }
                return;
        }
        max_off = kui_scrollback_max_off(sb);
        if (delta >= 0) {
                unsigned int amount = (unsigned int)delta;
                sb->view_offset = amount > max_off - (sb->view_offset > max_off
                        ? max_off : sb->view_offset) ? max_off
                        : sb->view_offset + amount;
                return;
        }
        next = (int)sb->view_offset + delta;
        sb->view_offset = next > 0 ? (unsigned int)next : 0;
}

void kui_clear_output(void) {
	if (!g_prog_data) return;
	kui_scrollback_reset();
	kui_render_page();
}

static void kui_add_vfmt(int do_render, const char *fmt, va_list ap) {
        ui_event_t event = {0};
        if (!g_prog_data || !fmt) return;
        event.type = UI_EVENT_OUTPUT;
        (void)vsnprintf(event.output, sizeof(event.output), fmt, ap);
        // @@ A barrier preserves log order relative to direct command logging.
        (void)kui_post_event_sync(&event);
        if (do_render) kui_render_page();
}

void kui_add_line(const char *fmt, ...) {
        va_list ap;
        if (!g_prog_data || !fmt) return;
        va_start(ap, fmt);
        kui_add_vfmt(0, fmt, ap);
        va_end(ap);
}

void kui_add_line_and_render(const char *fmt, ...) {
        va_list ap;
        if (!g_prog_data || !fmt) return;
        va_start(ap, fmt);
        kui_add_vfmt(1, fmt, ap);
        va_end(ap);
}

void kui_set_churning(const char *fmt, ...) {
        ui_event_t event = {0};
        va_list ap;
        if (!fmt) return;
        event.type = UI_EVENT_CHURNING;
        va_start(ap, fmt);
        (void)vsnprintf(event.output, sizeof(event.output), fmt, ap);
        va_end(ap);
        (void)kui_post_event_sync(&event);
}

void kui_clear_churning(void) {
        ui_event_t event = {0};
        event.type = UI_EVENT_CHURNING_CLEAR;
        (void)kui_post_event_sync(&event);
}

void kui_flush_log(void) {
        ui_event_t event = {0};
        if (!g_prog_data) return;
        event.type = UI_EVENT_FLUSH;
        (void)kui_post_event_sync(&event);
}

void kui_frame_start(void) {
        ui_event_t event = {0};
        if (!g_prog_data) return;
        event.type = UI_EVENT_FRAME_START;
        (void)kui_post_event_sync(&event);
}

kui_display_mode_t kui_display_mode_get(void) {
        ui_event_t event = {0};
        event.type = UI_EVENT_MODE_GET;
        return (kui_display_mode_t)kui_post_event_sync(&event);
}

int kui_display_mode_set(kui_display_mode_t mode) {
        ui_event_t event = {0};
        if (mode != KUI_DISPLAY_CONTINUOUS && mode != KUI_DISPLAY_FRAME)
                return -1;
        event.type = UI_EVENT_MODE;
        event.value = mode;
        return kui_post_event_sync(&event);
}
