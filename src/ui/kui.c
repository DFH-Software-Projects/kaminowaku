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
	(void)write(STDOUT_FILENO, "\x1b[?1000h", 8);
	(void)write(STDOUT_FILENO, "\x1b[?1002h", 8);
	(void)write(STDOUT_FILENO, "\x1b[?1006h", 8);
}

static void kui_mouse_disable(void) {
	(void)write(STDOUT_FILENO, "\x1b[?1006l", 8);
	(void)write(STDOUT_FILENO, "\x1b[?1002l", 8);
	(void)write(STDOUT_FILENO, "\x1b[?1000l", 8);
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
        unsigned int total;
        unsigned int i;
        unsigned int sum;
        if (!sb) return 0;
        total = sb->line_count;
        sum = 0;
        for (i = 0; i < total; i++) {
                unsigned int physical = (sb->head + i) % KUI_MAX_SCROLL_LINES;
                sum += kui_line_wrap_rows(sb->lines[physical], cols);
        }
        return sum;
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
                // @@ Desired rows are already blank in the virtual screen.
                if (KUI_SCREEN_ACTIVE != ISTRUE)
                        for (i = 0; i < window_h; i++)
                                kui_paint_row(content_top + i, "", 0, "", 0);
                return 0;
        }
        /* Physical row accounting */
        total_rows = kui_scrollback_total_rows(sb, cols);
        if (total_rows > window_h) max_off_rows = total_rows - window_h;
        else max_off_rows = 0;
        offset_rows = sb->view_offset;
        if (offset_rows > max_off_rows) offset_rows = max_off_rows;
        /* Interpret view_offset as physical rows from bottom */
        bottom_row_idx = (total_rows == 0) ? 0 : (total_rows - 1);
        if (offset_rows <= bottom_row_idx) bottom_row_idx -= offset_rows;
        else bottom_row_idx = 0;
        if (bottom_row_idx + 1 > window_h) first_row_idx = bottom_row_idx + 1 - window_h;
        else first_row_idx = 0;
        /*
         * Map first_row_idx to (start_logical, start_pos) by scanning logical lines from the top
         * and subtracting their wrapped row counts until we land inside the target line.
         */
        {
                unsigned int acc = 0;
                for (i = 0; i < total_lines; i++) {
                        unsigned int physical = (sb->head + i) % KUI_MAX_SCROLL_LINES;
                        const char *s = sb->lines[physical];
                        unsigned int rows = kui_line_wrap_rows(s, cols);
                        if (acc + rows > first_row_idx) {
                                /* We start inside this logical line */
                                unsigned int inside = first_row_idx - acc;
                                size_t n = strnlen(s ? s : "", KUI_MAX_SCROLL_COLS);
                                size_t pos = 0;
                                unsigned int seg = 0;
                                /* advance 'inside' segments to find byte start */
                                while (pos < n && seg < inside) {
                                        size_t end = kui_wrap_find_end(s, n, pos, cols);
                                        if (end <= pos) end = pos + 1;
                                        pos = end;
                                        while (pos < n && kui_is_space_byte((unsigned char)s[pos])) pos++;
                                        seg++;
                                }
                                start_logical = i;
                                start_pos = pos;
                                break;
                        }
                        acc += rows;
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
        unsigned int max_off;
        char tmp[KUI_MAX_SCROLL_COLS];
        size_t len;
        if (!g_prog_data || !line) return;
        sb = &g_prog_data->kui_scrollback;
        len = strnlen(line, KUI_MAX_SCROLL_COLS - 1);
        if (len > 0 && line[len - 1] == '\n') len--;
        memcpy(tmp, line, len);
        tmp[len] = 0x00;
        if (sb->line_count < KUI_MAX_SCROLL_LINES) {
                idx = (sb->head + sb->line_count) % KUI_MAX_SCROLL_LINES;
                sb->line_count++;
        } else {
                idx = sb->head;
                sb->head = (sb->head + 1) % KUI_MAX_SCROLL_LINES;
                if (sb->view_offset > 0)
                        sb->view_offset--;
        }

        memcpy(sb->lines[idx], tmp, len);
        sb->lines[idx][len] = 0x00;
        max_off = (sb->line_count > 0) ? (sb->line_count - 1) : 0;
        if (sb->view_offset > max_off)
                sb->view_offset = max_off;
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
        if (!fp) return;
        kui_log_frame_header_if_needed(fp);
        fprintf(fp, "%s\n", line);
        if (g_prog_data->debug_flag == ISTRUE) fflush(fp);
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

// @@ Synchronous event consumption; Phase 4 moves this pump to a dedicated thread.
static void kui_dispatch_events(void) {
        ui_event_t event;
        if (KUI_PUMPING == ISTRUE) return;
        KUI_PUMPING = ISTRUE;
        while (ui_events_next(&event)) {
                switch (event.type) {
                        case UI_EVENT_OUTPUT:
                                kui_sink_line(event.output);
                                break;
                        case UI_EVENT_SCROLL:
                                if (g_prog_data) {
                                        kui_scrollback_scroll_by(
                                                &g_prog_data->kui_scrollback,
                                                event.scroll_rows
                                        );
                                        kui_render_page();
                                }
                                break;
                        case UI_EVENT_INPUT:
                                snprintf(KUI_INPUT_TEXT, sizeof(KUI_INPUT_TEXT), "%s", event.input);
                                snprintf(KUI_INPUT_PROMPT, sizeof(KUI_INPUT_PROMPT), "%s", event.prompt);
                                KUI_INPUT_CURSOR = event.cursor;
                                if (KUI_INPUT_ACTIVE == ISTRUE)
                                        kui_input_draw(KUI_INPUT_PROMPT, KUI_INPUT_TEXT, (int)KUI_INPUT_CURSOR);
                                break;
                        default:
                                break;
                }
        }
        KUI_PUMPING = ISFALSE;
}

// @@ Drain when full. The synchronous Phase 1 transport never drops events.
static void kui_post_event(const ui_event_t *event) {
        if (ui_events_post(event) == 0) return;
        kui_dispatch_events();
        if (ui_events_post(event) != 0) {
                // This should be unreachable with one synchronous producer.
                FILE *fp = g_prog_data ? (FILE *)g_prog_data->log : NULL;
                if (fp) fprintf(fp, "[x] KUI event queue overflow.\n");
        }
}

void kui_input_begin(void) {
        KUI_INPUT_ACTIVE = ISTRUE;
        KUI_INPUT_DIRTY = ISTRUE;
        kui_input_anchor();
}

void kui_input_update(const char *prompt, const char *input, unsigned int cursor) {
        ui_event_t event = {0};
        event.type = UI_EVENT_INPUT;
        if (prompt) snprintf(event.prompt, sizeof(event.prompt), "%s", prompt);
        if (input) snprintf(event.input, sizeof(event.input), "%s", input);
        event.cursor = cursor;
        kui_post_event(&event);
        kui_dispatch_events();
}

void kui_input_end(void) {
        KUI_INPUT_ACTIVE = ISFALSE;
        KUI_INPUT_DIRTY = ISTRUE;
}

void kui_input_scroll(int rows) {
        ui_event_t event = {0};
        event.type = UI_EVENT_SCROLL;
        event.scroll_rows = rows;
        kui_post_event(&event);
        kui_dispatch_events();
}

void kui_input_refresh_page(void) {
        kui_render_page();
}

void kui_input_bell(void) {
        (void)write(STDOUT_FILENO, "\a", 1);
}

/* ------------------------------------------------------------------------ */
/* Rendering                                                                */
/* ------------------------------------------------------------------------ */

static int KUI_SMALL = ISFALSE;

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

void kui_render_page(void) {
        unsigned rows, cols, old_rows, old_cols, content_top, content_bottom, prompt_row;
        int8_t first_time, size_changed;
        int banner_rows;
        KUI_SCROLLBACK *sb;

        kui_dispatch_events();
        if (!g_prog_data) return;
        kui_get_winsize(&rows, &cols);
        old_rows = g_prog_data->term_rows;
        old_cols = g_prog_data->term_cols;
        first_time = (old_rows == 0 && old_cols == 0);
        size_changed = (first_time || old_rows != rows || old_cols != cols);
        if (size_changed) {
                g_prog_data->term_rows = rows;
                g_prog_data->term_cols = cols;
                banner_reset();
                ui_screen_invalidate(&KUI_SCREEN);
                KUI_INPUT_DIRTY = ISTRUE;
                kui_reset_scroll_region();
        }
        if (rows < KUI_MIN_ROWS) {
                ui_screen_invalidate(&KUI_SCREEN);
                kui_render_small(rows, cols);
                return;
        }
        if (KUI_SMALL == ISTRUE) {
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

        KUI_SCREEN_ACTIVE = ISFALSE;
        KUI_SCREEN_FAILED = ISFALSE;
        if (KUI_SCREEN_READY == ISTRUE
                && ui_screen_begin(&KUI_SCREEN, rows, cols, content_top, rows - 1) == 0)
                KUI_SCREEN_ACTIVE = ISTRUE;

        sb = &g_prog_data->kui_scrollback;
        if (sb->line_count == 0 && sb->head == 0 && sb->view_offset == 0)
                kui_scrollback_init(sb);

        // @@ Retain Phase 1 offset semantics until Phase 3's wrap index.
        {
                unsigned int window_h = content_bottom - content_top + 1;
                unsigned int total = sb->line_count;
                unsigned int max_off = total > window_h ? total - window_h : 0;
                if (sb->view_offset > max_off) sb->view_offset = max_off;
        }

        (void)kui_scrollback_render(sb, content_top, content_bottom, cols);
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

/* ------------------------------------------------------------------------ */
/* Scrollback/Output/Enter&Exit + public API                                */
/* ------------------------------------------------------------------------ */

void kui_enter(_carry_forward * _prog_data) {
	if (_prog_data && !g_prog_data) g_prog_data = _prog_data;
        ui_events_reset();
        KUI_INPUT_ACTIVE = ISFALSE;
        KUI_INPUT_DIRTY = ISTRUE;
        KUI_SMALL = ISFALSE;
        if (ui_screen_init(&KUI_SCREEN, STDOUT_FILENO) == 0)
                KUI_SCREEN_READY = ISTRUE;
        memset(KUI_CHURNING, 0x00, sizeof(KUI_CHURNING));
        KUI_CHURNING_ACTIVE = ISFALSE;
	kui_mouse_enable();
	(void)write(STDOUT_FILENO, ANSI_ALT_SCREEN_ON, strlen(ANSI_ALT_SCREEN_ON));
	(void)write(STDOUT_FILENO, ANSI_CURSOR_HIDE, strlen(ANSI_CURSOR_HIDE));
	(void)write(STDOUT_FILENO, ANSI_CLEAR_SCREEN ANSI_CURSOR_HOME, sizeof(ANSI_CLEAR_SCREEN ANSI_CURSOR_HOME) - 1);
	banner_reset();
	fflush(stdout);
}

void kui_exit(void) {
        kui_dispatch_events();
        KUI_INPUT_ACTIVE = ISFALSE;
        KUI_SCREEN_ACTIVE = ISFALSE;
        ui_screen_destroy(&KUI_SCREEN);
        KUI_SCREEN_READY = ISFALSE;
	if (g_prog_data) {
		FILE * fp = (FILE *)g_prog_data->log;
		if (fp) fflush(fp);
		g_prog_data = NULL;
	}
        memset(KUI_CHURNING, 0x00, sizeof(KUI_CHURNING));
        KUI_CHURNING_ACTIVE = ISFALSE;
	kui_mouse_disable();
	kui_reset_scroll_region();
	(void)write(STDOUT_FILENO, ANSI_CLEAR_SCREEN ANSI_CURSOR_HOME, sizeof(ANSI_CLEAR_SCREEN ANSI_CURSOR_HOME) - 1);
	(void)write(STDOUT_FILENO, ANSI_CURSOR_SHOW, strlen(ANSI_CURSOR_SHOW));
	(void)write(STDOUT_FILENO, ANSI_ALT_SCREEN_OFF, strlen(ANSI_ALT_SCREEN_OFF));
	fflush(stdout);
}

void kui_scrollback_reset(void) {
        kui_dispatch_events();
	if (!g_prog_data) return;
	kui_scrollback_init(&g_prog_data->kui_scrollback);
}

static unsigned int kui_scrollback_max_off(const KUI_SCROLLBACK *sb) {
        if (!sb || sb->line_count == 0) return 0;
        return sb->line_count - 1;
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
        int cur;
        int next;
        if (!sb) return;
        max_off = kui_scrollback_max_off(sb);
        cur = (int)sb->view_offset;
        next = cur + delta;
        if (next < 0) next = 0;
        if (next > (int)max_off) next = (int)max_off;
        sb->view_offset = (unsigned int)next;
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
        kui_post_event(&event);
        // @@ Preserve immediate log ordering while rendering remains deferred.
        kui_dispatch_events();
        if (do_render)
                kui_render_page();
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
        va_list ap;
        if (!fmt) return;
        memset(KUI_CHURNING, 0x00, sizeof(KUI_CHURNING));
        va_start(ap, fmt);
        (void)vsnprintf(KUI_CHURNING, sizeof(KUI_CHURNING), fmt, ap);
        va_end(ap);
        KUI_CHURNING_ACTIVE = ISTRUE;
}

void kui_clear_churning(void) {
        memset(KUI_CHURNING, 0x00, sizeof(KUI_CHURNING));
        KUI_CHURNING_ACTIVE = ISFALSE;
}

void kui_flush_log(void) {
        kui_dispatch_events();
	if (!g_prog_data) return;
        FILE *fp;
	fp = (FILE *)g_prog_data->log;
	if (fp) fflush(fp);
}

void kui_frame_start(void) {
        kui_dispatch_events();
	if (!g_prog_data) return;
	g_prog_data->log_frame_start = ISTRUE;
}
