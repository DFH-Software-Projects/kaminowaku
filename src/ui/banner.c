// Copyright 2026 Jamison A. Drapeau
// Diff-draw static header + clean output region (no scrolling, no flicker).

#include "banner.h"
#include "helpers.h"
#include "kscan.h"
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <unistd.h>
#include <stdlib.h>
#include <locale.h>
#include <stdbool.h>
#include <stdarg.h>

// ─────────────────────────────────────────────────────────────────────────────
// Configuration (keeps your style/colors exactly)
// ─────────────────────────────────────────────────────────────────────────────
#ifndef TAB_STOP
#define TAB_STOP 8
#endif

// Keep your existing RGB/ANSI macros from helpers.h (e.g., RGB_COLOR_DARK_MAGENTA)
#define BORDER_COLOR RGB_COLOR_DARK_MAGENTA
#define BANNER_STYLE BORDER_HEAVY    // BORDER_LIGHT | BORDER_HEAVY | BORDER_DOUBLE | BORDER_SOLID

#ifndef USE_UTF8_BOX
#define USE_UTF8_BOX 1
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Terminal helpers
// ─────────────────────────────────────────────────────────────────────────────
static inline void ui_move(int row, int col) { printf("\x1b[%d;%dH", row, col); }
static inline void ui_clear_eol(void)       { fputs("\x1b[K", stdout); }

static int term_cols(void) {
        struct winsize ws = {0};
        int cols = 80;
        if (isatty(STDOUT_FILENO) && ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
                cols = (int)ws.ws_col;
        if (cols < 40) cols = 40;
        return cols;
}

static int utf8_ok(void) {
#if USE_UTF8_BOX
        const char *loc = setlocale(LC_ALL, "");
        if (!loc) return 0;
        return (strstr(loc, "UTF-8") || strstr(loc, "utf8")) ? 1 : 0;
#else
        return 0;
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// Border glyphs
// ─────────────────────────────────────────────────────────────────────────────
typedef enum {
        BORDER_LIGHT,
        BORDER_HEAVY,
        BORDER_DOUBLE,
        BORDER_SOLID
} border_style_t;

typedef struct {
        const char *top_left, *top_right, *bot_left, *bot_right;
        const char *mid_left, *mid_right;
        const char *h, *v;
        int solid_bar;
} border_chars;

static border_chars get_border(border_style_t style, int allow_utf8) {
        border_chars bc;
        if (!allow_utf8) {
                bc.top_left  = "+";  bc.top_right = "+";
                bc.bot_left  = "+";  bc.bot_right = "+";
                bc.mid_left  = "+";  bc.mid_right = "+";
                bc.h = "-";          bc.v = "|";
                bc.solid_bar = (style == BORDER_SOLID) ? 1 : 0;
                return bc;
        }
        switch (style) {
        case BORDER_HEAVY:
                bc.top_left="┏";bc.top_right="┓";bc.bot_left="┗";bc.bot_right="┛";
                bc.mid_left="┣";bc.mid_right="┫";bc.h="━";bc.v="┃";bc.solid_bar=0;break;
        case BORDER_DOUBLE:
                bc.top_left="╔";bc.top_right="╗";bc.bot_left="╚";bc.bot_right="╝";
                bc.mid_left="╠";bc.mid_right="╣";bc.h="═";bc.v="║";bc.solid_bar=0;break;
        case BORDER_SOLID:
                bc.top_left="█";bc.top_right="█";bc.bot_left="█";bc.bot_right="█";
                bc.mid_left="█";bc.mid_right="█";bc.h="█";bc.v="█";bc.solid_bar=1;break;
        case BORDER_LIGHT:
        default:
                bc.top_left="┌";bc.top_right="┐";bc.bot_left="└";bc.bot_right="┘";
                bc.mid_left="├";bc.mid_right="┤";bc.h="─";bc.v="│";bc.solid_bar=0;break;
        }
        return bc;
}

// ─────────────────────────────────────────────────────────────────────────────
// ANSI-aware line building (fixed visual width inside frame)
// ─────────────────────────────────────────────────────────────────────────────
static size_t visible_width_noansi(const char *s) {
        size_t w = 0;
        const unsigned char *p = (const unsigned char *)s;
        while (*p) {
                if (*p == '\x1b') {
                        p++;
                        if (*p == '[') {
                                p++;
                                while (*p && !(*p >= 0x40 && *p <= 0x7E)) p++;
                                if (*p) p++;
                        }
                        continue;
                }
                if (*p == '\t') {
                        size_t to_next = TAB_STOP - (w % TAB_STOP);
                        w += to_next; p++; continue;
                }
                if ((*p & 0xC0) == 0x80) {
                        p++;
                        continue;
                }
                w++; p++;
        }
        return w;
}

static void build_row_exact(int cols, const border_chars *bc, const char *content, char *out, size_t outsz) {
        if (!out || outsz == 0) return;

        int inner = cols - 3;
        if (inner < 0) inner = 0;

        char buf[8192];
        int  blen = 0;
        int  width = 0;

        #define APPEND_CHAR(ch) do { if (blen + 1 < (int)sizeof(buf)) buf[blen++] = (char)(ch); } while (0)
        #define APPEND_STR(cs) do { const char *qq=(cs); while(*qq && blen + 1 < (int)sizeof(buf)) buf[blen++]=*qq++; } while (0)
        #define PAD_SPACE()  do { if (width < inner) { APPEND_CHAR(' '); width++; } } while (0)

        size_t in_len = strlen(content);
        while (in_len && (content[in_len - 1] == '\n' || content[in_len - 1] == '\r')) in_len--;
        const unsigned char *p = (const unsigned char *)content;
        const unsigned char *end = p + in_len;

        // left wall
        APPEND_STR(BORDER_COLOR); APPEND_STR(bc->v); APPEND_STR(ANSI_COLOR_RESET);

        while (p < end && width < inner) {
                if (*p == '\x1b') {
                        const unsigned char *start = p++;
                        if (p < end && *p == '[') {
                                p++;
                                while (p < end && !(*p >= 0x40 && *p <= 0x7E)) p++;
                                if (p < end) p++;
                        }
                        for (const unsigned char *q = start; q < p; q++) APPEND_CHAR(*q);
                        continue;
                }
                if (*p == '\t') {
                        int to_next = TAB_STOP - (width % TAB_STOP);
                        while (to_next-- > 0 && width < inner) PAD_SPACE();
                        p++;
                        continue;
                }
                unsigned char ch = *p++;
                APPEND_CHAR(ch);
                if ((ch & 0xC0) != 0x80)
                        width++;
        }
        while (width < inner) PAD_SPACE();

        // right wall
        APPEND_STR(BORDER_COLOR); APPEND_STR(bc->v); APPEND_STR(ANSI_COLOR_RESET);
        APPEND_CHAR('\0');

        // copy to out (and ensure NUL)
        snprintf(out, outsz, "%s", buf);

        #undef APPEND_CHAR
        #undef APPEND_STR
        #undef PAD_SPACE
}

static void build_horizontal(int cols, const border_chars *bc, int top, char *out, size_t outsz) {
        if (!out || outsz == 0) return;

        if (bc->solid_bar) {
                char tmp[8192]; tmp[0] = '\0';
                size_t used = 0;
                used += (size_t)snprintf(tmp + used, sizeof(tmp) - used, "%s", BORDER_COLOR);
                for (int i = 0; i < cols && used + 8 < sizeof(tmp); i++)
                        used += (size_t)snprintf(tmp + used, sizeof(tmp) - used, "%s", bc->h);
                (void)snprintf(out, outsz, "%s%s", tmp, ANSI_COLOR_RESET);
                return;
        }

        // HERE 2 --> 3
        int inner = cols - 3; if (inner < 0) inner = 0;
        char tmp[8192]; size_t used = 0;
        used += (size_t)snprintf(tmp + used, sizeof(tmp) - used, "%s", BORDER_COLOR);
        used += (size_t)snprintf(tmp + used, sizeof(tmp) - used, "%s", top ? bc->top_left : bc->bot_left);
        for (int i = 0; i < inner && used + 8 < sizeof(tmp); i++)
                used += (size_t)snprintf(tmp + used, sizeof(tmp) - used, "%s", bc->h);
        used += (size_t)snprintf(tmp + used, sizeof(tmp) - used, "%s", top ? bc->top_right : bc->bot_right);
        (void)snprintf(out, outsz, "%s%s", tmp, ANSI_COLOR_RESET);
}

static void build_separator(int cols, const border_chars *bc, char *out, size_t outsz) {
        if (!out || outsz == 0) return;

        if (bc->solid_bar) {
                char tmp[8192]; tmp[0] = '\0';
                size_t used = 0;
                used += (size_t)snprintf(tmp + used, sizeof(tmp) - used, "%s", BORDER_COLOR);
                for (int i = 0; i < cols && used + 8 < sizeof(tmp); i++)
                        used += (size_t)snprintf(tmp + used, sizeof(tmp) - used, "%s", bc->h);
                (void)snprintf(out, outsz, "%s%s", tmp, ANSI_COLOR_RESET);
                return;
        }

        int inner = cols - 3; if (inner < 0) inner = 0;
        char tmp[8192]; size_t used = 0;
        used += (size_t)snprintf(tmp + used, sizeof(tmp) - used, "%s", BORDER_COLOR);
        used += (size_t)snprintf(tmp + used, sizeof(tmp) - used, "%s", bc->mid_left);
        for (int i = 0; i < inner && used + 8 < sizeof(tmp); i++)
                used += (size_t)snprintf(tmp + used, sizeof(tmp) - used, "%s", bc->h);
        used += (size_t)snprintf(tmp + used, sizeof(tmp) - used, "%s", bc->mid_right);
        (void)snprintf(out, outsz, "%s%s", tmp, ANSI_COLOR_RESET);
}

// ─────────────────────────────────────────────────────────────────────────────
// Previous frame store for diffing
// ─────────────────────────────────────────────────────────────────────────────
#define MAX_BANNER_LINES 32
#define MAX_LINE_COLS    8192

static char prev_lines[MAX_BANNER_LINES][MAX_LINE_COLS];
static int  prev_count = 0;
static int  prev_cols  = 0;

void banner_reset(void) {
        prev_count = 0;
        prev_cols  = 0;
        for (int i = 0; i < MAX_BANNER_LINES; i++) {
                prev_lines[i][0] = '\0';
        }
}

// Push helpers: write into curr_lines[*n] and advance n.
static inline void push_raw(char curr_lines[][MAX_LINE_COLS], int *n, const char *s) {
        snprintf(curr_lines[*n], MAX_LINE_COLS, "%s", s ? s : "");
        (*n)++;
}

static inline void push_border_top(int cols, const border_chars *bc, char curr_lines[][MAX_LINE_COLS], int *n) {
        build_horizontal(cols, bc, /*top=*/1, curr_lines[*n], MAX_LINE_COLS);
        (*n)++;
}

static inline void push_border_bottom(int cols, const border_chars *bc, char curr_lines[][MAX_LINE_COLS], int *n) {
        build_horizontal(cols, bc, /*top=*/0, curr_lines[*n], MAX_LINE_COLS);
        (*n)++;
}

static inline void push_separator(int cols, const border_chars *bc, char curr_lines[][MAX_LINE_COLS], int *n) {
        build_separator(cols, bc, curr_lines[*n], MAX_LINE_COLS);
        (*n)++;
}

static inline void push_row_exactf(int cols, const border_chars *bc, char curr_lines[][MAX_LINE_COLS], int *n, const char *fmt, ...) {
        char linebuf[MAX_LINE_COLS];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(linebuf, sizeof(linebuf), fmt, ap);
        va_end(ap);
        build_row_exact(cols, bc, linebuf, curr_lines[*n], MAX_LINE_COLS);
        (*n)++;
}

static inline void push_row_exact(int cols, const border_chars *bc, char curr_lines[][MAX_LINE_COLS], int *n, const char *s) {
        build_row_exact(cols, bc, s ? s : "", curr_lines[*n], MAX_LINE_COLS);
        (*n)++;
}

// Place one field against the left side and another against the right side.
// build_row_exact() supplies the final one-space margin before the right wall.
static void push_row_split_exact(
        int cols,
        const border_chars *bc,
        char curr_lines[][MAX_LINE_COLS],
        int *n,
        const char *left,
        const char *right
) {
        char content[MAX_LINE_COLS];
        size_t left_width;
        size_t right_width;
        size_t pos;
        int inner;
        int gap;

        if (!bc || !curr_lines || !n || *n >= MAX_BANNER_LINES || !left || !right)
                return;

        inner = cols - 3;
        if (inner < 0)
                inner = 0;

        left_width = visible_width_noansi(left);
        right_width = visible_width_noansi(right);

        // Reserve one trailing space before the right border.
        gap = inner - (int)left_width - (int)right_width - 1;
        if (gap < 1)
                gap = 1;

        pos = (size_t)snprintf(content, sizeof(content), "%s", left);
        if (pos >= sizeof(content))
                pos = sizeof(content) - 1;

        while (gap-- > 0 && pos + 1 < sizeof(content))
                content[pos++] = ' ';

        if (pos < sizeof(content))
                snprintf(content + pos, sizeof(content) - pos, "%s", right);

        build_row_exact(cols, bc, content, curr_lines[*n], MAX_LINE_COLS);
        (*n)++;
}

// Compact decimal byte formatter: 999B, 1.00K, 12.3M, 123G, etc.
static void banner_format_bytes(uint64_t bytes, char *out, size_t outsz) {
        static const char UNITS[] = {'B', 'K', 'M', 'G', 'T'};
        long double scaled;
        int unit;

        if (!out || outsz == 0)
                return;

        if (bytes < 1000) {
                snprintf(out, outsz, "%" PRIu64 "B", bytes);
                return;
        }

        scaled = (long double)bytes;
        unit = 0;

        while (scaled >= 1000.0L && unit < 4) {
                scaled /= 1000.0L;
                unit++;
        }

        // Promote values that would otherwise round to an awkward 1000K/1000M.
        while (scaled >= 999.5L && unit < 4) {
                scaled /= 1000.0L;
                unit++;
        }

        if (scaled >= 100.0L)
                snprintf(out, outsz, "%.0Lf%c", scaled, UNITS[unit]);
        else if (scaled >= 10.0L)
                snprintf(out, outsz, "%.1Lf%c", scaled, UNITS[unit]);
        else
                snprintf(out, outsz, "%.2Lf%c", scaled, UNITS[unit]);
}

// Yellow marks profile-managed selection rather than a concrete OS interface.
static const char * banner_interface_color(const unsigned char *interface_name) {
        if (!interface_name || interface_name[0] == 0x00)
                return RGB_COLOR_BRIGHT_YELLOW;

        if (
                strcmp((const char *)interface_name, "auto") == MATCH
                ||
                strcmp((const char *)interface_name, "same") == MATCH
        ) {
                return RGB_COLOR_BRIGHT_YELLOW;
        }

        if (if_nametoindex((const char *)interface_name) != 0)
                return RGB_COLOR_BRIGHT_GREEN;

        return RGB_COLOR_PINKISH_RED;
}

static const char * banner_interface_name(const unsigned char *interface_name) {
        if (!interface_name || interface_name[0] == 0x00)
                return "UNSET";

        return (const char *)interface_name;
}

// ─────────────────────────────────────────────────────────────────────────────
// Plain build and push for tite header
// ─────────────────────────────────────────────────────────────────────────────
// Plain (no-border) row builder: ANSI-safe, padded/truncated to cols.
static void build_plain_exact(int cols, const char *content, char *out, size_t outsz) {
        if (!out || outsz == 0)
                return;

        int inner = cols > 1 ? cols - 1 : cols; // Avoid pending terminal auto-wrap.
        if (inner < 0)
                inner = 0;

        char buf[8192];
        int  blen  = 0;
        int  width = 0;

#define APPEND_CHAR(ch) do { if (blen + 1 < (int)sizeof(buf)) buf[blen++] = (char)(ch); } while (0)
#define APPEND_STR(cs)  do { const char *qq = (cs); while (*qq && blen + 1 < (int)sizeof(buf)) buf[blen++] = *qq++; } while (0)
#define PAD_SPACE()     do { if (width < inner) { APPEND_CHAR(' '); width++; } } while (0)

        size_t in_len = strlen(content);
        while (in_len && (content[in_len - 1] == '\n' || content[in_len - 1] == '\r'))
                in_len--;

        const unsigned char *p   = (const unsigned char *)content;
        const unsigned char *end = p + in_len;

        while (p < end && width < inner) {
                if (*p == '\x1b') {
                        const unsigned char *start = p++;
                        if (p < end && *p == '[') {
                                p++;
                                while (p < end && !(*p >= 0x40 && *p <= 0x7E))
                                        p++;
                                if (p < end)
                                        p++;
                        }
                        for (const unsigned char *q = start; q < p; q++)
                                APPEND_CHAR(*q);
                        continue;
                }
                if (*p == '\t') {
                        int to_next = TAB_STOP - (width % TAB_STOP);
                        while (to_next-- > 0 && width < inner)
                                PAD_SPACE();
                        p++;
                        continue;
                }
                APPEND_CHAR(*p++);
                width++;
        }

        while (width < inner)
                PAD_SPACE();

        APPEND_CHAR('\0');
        snprintf(out, outsz, "%s", buf);

#undef APPEND_CHAR
#undef APPEND_STR
#undef PAD_SPACE
}

// Convenience helper: push formatted plain line into curr_lines and bump n.
static void push_plain_exactf(int cols, char curr_lines[][MAX_LINE_COLS], int *n, const char *fmt, ...) {
        if (!curr_lines || !n || *n >= MAX_BANNER_LINES)
                return;

        char tmp[MAX_LINE_COLS];
        tmp[0] = '\0';

        va_list ap;
        va_start(ap, fmt);
        vsnprintf(tmp, sizeof(tmp), fmt, ap);
        va_end(ap);

        build_plain_exact(cols, tmp, curr_lines[*n], MAX_LINE_COLS);
        (*n)++;
}


// ─────────────────────────────────────────────────────────────────────────────
// Build the banner lines for current state (into curr_lines / returns count)
// ─────────────────────────────────────────────────────────────────────────────
static int build_banner_lines(_carry_forward * _prog_data, int cols, char curr_lines[][MAX_LINE_COLS]) {
        int allow_utf8 = utf8_ok();
        border_chars bc = get_border(BANNER_STYLE, allow_utf8);

        int n = 0;

        // Title Header (plain, no border, but diff-drawn)
        push_plain_exactf(
                cols,
                curr_lines,
                &n,
                ANSI_COLOR_MAGENTA "[" ANSI_COLOR_RESET
                "KAMI NO WAKU " ANSI_COLOR_CYAN VERSION ANSI_COLOR_RESET
                ANSI_COLOR_MAGENTA "]" ANSI_COLOR_RESET
                " Use 'help' if you're stuck."
        );

        // Top border
        push_border_top(cols, &bc, curr_lines, &n);

        // Runtime interface and wire counters
        {
                char interface_field[256];
                char counter_field[256];
                char tx_total[32];
                char rx_total[32];
                const unsigned char *interface_name = _prog_data->gprof.tx_interface;
                const char *net_color = (_prog_data->nosix_net != NULL)
                        ? RGB_COLOR_BRIGHT_GREEN
                        : RGB_COLOR_SOFT_GREY;

                banner_format_bytes(_prog_data->total_tx_bytes, tx_total, sizeof(tx_total));
                banner_format_bytes(_prog_data->total_rx_bytes, rx_total, sizeof(rx_total));

                snprintf(
                        interface_field,
                        sizeof(interface_field),
                        " " ANSI_COLOR_CYAN "IF:" ANSI_COLOR_RESET " %s%s" ANSI_COLOR_RESET,
                        banner_interface_color(interface_name),
                        banner_interface_name(interface_name)
                );

                snprintf(
                        counter_field,
                        sizeof(counter_field),
                        "%s●" ANSI_COLOR_RESET "    "
                        ANSI_COLOR_CYAN "TX:" ANSI_COLOR_RESET " %s    "
                        ANSI_COLOR_CYAN "RX:" ANSI_COLOR_RESET " %s",
                        net_color,
                        tx_total,
                        rx_total
                );

                push_row_split_exact(
                        cols,
                        &bc,
                        curr_lines,
                        &n,
                        interface_field,
                        counter_field
                );
        }

        push_separator(cols, &bc, curr_lines, &n);

        // Body
        ///////////////////////////////////////////////////////////////////////////////////////////////////////[BANNER_START]
        if (_prog_data->active_project[0] != 0x00 && _prog_data->active_project_active_target_context == ISFALSE) {
                push_row_exactf(cols, &bc, curr_lines, &n, " PROJECT:\t\t" RGB_COLOR_BRIGHT_GREEN "%s" ANSI_COLOR_RESET, _prog_data->active_project);
                if (_prog_data->active_project_target_count < 1) {
                        _prog_data->active_project_has_targets = ISFALSE;
                }
                if (_prog_data->active_project_has_targets == ISFALSE) {
                        push_row_exact(cols, &bc, curr_lines, &n,
                                       " TARGETS:\t\t" RGB_COLOR_BRIGHT_YELLOW "NONE" ANSI_COLOR_RESET);
                } else {
                        char TARGETS_COUNT_CONVERSION[64];
                        snprintf(
                                TARGETS_COUNT_CONVERSION, sizeof(TARGETS_COUNT_CONVERSION), "%" PRIu64, _prog_data->active_project_target_count
                        );
                        push_row_exactf(cols, &bc, curr_lines, &n, " TARGETS:\t\t" RGB_COLOR_BRIGHT_GREEN "%s" ANSI_COLOR_RESET, TARGETS_COUNT_CONVERSION);
                }

                if (
                        _prog_data->active_project_has_been_scanned == ISFALSE
                        ||
                        _prog_data->active_project_last_scan_ns == 0
                ) {
                        push_row_exact(cols, &bc, curr_lines, &n, "    SCAN:\t\t" RGB_COLOR_BRIGHT_YELLOW "NEVER" ANSI_COLOR_RESET);
                } else {
                        char LAST_SCAN[64];
                        kscan_format_ns(
                                _prog_data->active_project_last_scan_ns,
                                LAST_SCAN,
                                sizeof(LAST_SCAN)
                        );
                        push_row_exactf(
                                cols,
                                &bc,
                                curr_lines,
                                &n,
                                "    SCAN:\t\t" RGB_COLOR_BRIGHT_GREEN "%s" ANSI_COLOR_RESET,
                                LAST_SCAN
                        );
                }
        } else if (_prog_data->active_project[0] != 0x00 && _prog_data->active_project_active_target_context == ISTRUE) {
                //push_row_exactf(cols, &bc, curr_lines, &n, " PROJECT:\t\t" RGB_COLOR_BRIGHT_GREEN "%s" ANSI_COLOR_RESET, _prog_data->active_project);
                //push_row_exactf(cols, &bc, curr_lines, &n, "");
                //push_row_exactf(cols, &bc, curr_lines, &n, ANSI_COLOR_CYAN "┏━━━━━━━━━━━━━━━━━━┓" ANSI_COLOR_RESET);
                //push_row_exactf(cols, &bc, curr_lines, &n, 
                //        ANSI_COLOR_CYAN "┃" ANSI_COLOR_RESET " %s " ANSI_COLOR_CYAN "┃" ANSI_COLOR_RESET
                //        , _prog_data->active_project_active_target->TID
                //);
                //push_row_exactf(cols, &bc, curr_lines, &n, ANSI_COLOR_CYAN "┗━━━━━━━━━━━━━━━━━━┛" ANSI_COLOR_RESET);
                if (_prog_data->active_project_active_target->PETAL->URL[0] != 0x00) {
                        push_row_exactf(cols, &bc, curr_lines, &n, 
                              ANSI_COLOR_CYAN
                              "    URL"
                              ANSI_COLOR_RESET
                              ":\t%s"
                              , _prog_data->active_project_active_target->PETAL->URL  
                        );
                } else {
                        push_row_exactf(cols, &bc, curr_lines, &n, 
                              ANSI_COLOR_CYAN
                              "    URL"
                              ANSI_COLOR_RESET
                              ":\t"
                              RGB_COLOR_BRIGHT_YELLOW
                              "Not Set"
                              ANSI_COLOR_RESET
                        );
                }
                if (_prog_data->active_project_active_target->PETAL->IPV4[0] != 0x00) {
                        push_row_exactf(cols, &bc, curr_lines, &n, 
                              ANSI_COLOR_CYAN
                              "   IPv4"
                              ANSI_COLOR_RESET
                              ":\t%s"
                              , _prog_data->active_project_active_target->PETAL->IPV4
                        );
                } else {
                        push_row_exactf(cols, &bc, curr_lines, &n, 
                              ANSI_COLOR_CYAN
                              "   IPv4"
                              ANSI_COLOR_RESET
                              ":\t"
                              RGB_COLOR_BRIGHT_YELLOW
                              "Not Set"
                              ANSI_COLOR_RESET
                        );
                }
                if (_prog_data->active_project_active_target->PETAL->IPV6[0] != 0x00) {
                        push_row_exactf(cols, &bc, curr_lines, &n, 
                              ANSI_COLOR_CYAN
                              "   IPv6"
                              ANSI_COLOR_RESET
                              ":\t%s"
                              , _prog_data->active_project_active_target->PETAL->IPV6
                        );
                } else {
                        push_row_exactf(cols, &bc, curr_lines, &n, 
                              ANSI_COLOR_CYAN
                              "   IPv6"
                              ANSI_COLOR_RESET
                              ":\t"
                              RGB_COLOR_BRIGHT_YELLOW
                              "Not Set"
                              ANSI_COLOR_RESET
                        );
                }
                if (_prog_data->active_project_active_target->PETAL->MAC[0] != 0x00) {
                        push_row_exactf(cols, &bc, curr_lines, &n, 
                              ANSI_COLOR_CYAN
                              "    MAC"
                              ANSI_COLOR_RESET
                              ":\t%s"
                              , _prog_data->active_project_active_target->PETAL->MAC
                        );
                } else {
                        push_row_exactf(cols, &bc, curr_lines, &n, 
                              ANSI_COLOR_CYAN
                              "    MAC"
                              ANSI_COLOR_RESET
                              ":\t"
                              RGB_COLOR_BRIGHT_YELLOW
                              "Not Set"
                              ANSI_COLOR_RESET
                        );
                }
                if (_prog_data->active_project_active_target->PETAL->NOTE[0] != 0x00) {
                        push_row_exactf(cols, &bc, curr_lines, &n, 
                              ANSI_COLOR_CYAN
                              "  NOTES"
                              ANSI_COLOR_RESET
                              ":\t%s"
                              , _prog_data->active_project_active_target->PETAL->NOTE
                        );
                } else {
                        push_row_exactf(cols, &bc, curr_lines, &n, 
                              ANSI_COLOR_CYAN
                              "  NOTES"
                              ANSI_COLOR_RESET
                              ":\t"
                              RGB_COLOR_BRIGHT_YELLOW
                              "None"
                              ANSI_COLOR_RESET
                        );
                }

                // @@ Target context reports the active target's most recent scan
                if (
                        kscan_data_valid(_prog_data->active_project_active_target->PETAL) == ISFALSE
                        ||
                        _prog_data->active_project_active_target->PETAL->SCAN.LAST_SCAN_NS == 0
                ) {
                        push_row_exactf(cols, &bc, curr_lines, &n,
                              ANSI_COLOR_CYAN
                              "   SCAN"
                              ANSI_COLOR_RESET
                              ":\t"
                              RGB_COLOR_BRIGHT_YELLOW
                              "NEVER"
                              ANSI_COLOR_RESET
                        );
                } else {
                        char LAST_SCAN[64];
                        kscan_format_ns(
                                _prog_data->active_project_active_target->PETAL->SCAN.LAST_SCAN_NS,
                                LAST_SCAN,
                                sizeof(LAST_SCAN)
                        );
                        push_row_exactf(cols, &bc, curr_lines, &n,
                              ANSI_COLOR_CYAN
                              "   SCAN"
                              ANSI_COLOR_RESET
                              ":\t"
                              RGB_COLOR_BRIGHT_GREEN
                              "%s"
                              ANSI_COLOR_RESET
                              , LAST_SCAN
                        );
                }
        } else {
                push_row_exact(cols, &bc, curr_lines, &n, RGB_COLOR_BRIGHT_YELLOW " No project selected..." ANSI_COLOR_RESET);
        }

        // Separator
        push_separator(cols, &bc, curr_lines, &n);
        ///////////////////////////////////////////////////////////////////////////////////////////////////////[BANNER_END]
        
        // Last command (cyan caret preserved)
        if (_prog_data->cmd_last[0]) {
                push_row_exactf(cols, &bc, curr_lines, &n,
                                ANSI_COLOR_CYAN " ^ " ANSI_COLOR_RESET "%s",
                                (const char *)_prog_data->cmd_last);
        } else {
                push_row_exact(cols, &bc, curr_lines, &n,
                               ANSI_COLOR_CYAN " ^ " ANSI_COLOR_RESET);
        }

        // Bottom border
        push_border_bottom(cols, &bc, curr_lines, &n);

        return n;
}

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────
int banner(_carry_forward * _prog_data) {
        int cols = term_cols();

        // Build current banner lines
        char curr_lines[MAX_BANNER_LINES][MAX_LINE_COLS];
        for (int i = 0; i < MAX_BANNER_LINES; i++) curr_lines[i][0] = '\0';
        int curr_count = build_banner_lines(_prog_data, cols, curr_lines);

        // @@ The KUI renderer owns cursor visibility and placement.

        // If terminal width changed, force repaint all
        bool repaint_all = (prev_cols != cols) || (prev_count == 0);
        if (repaint_all) {
                prev_cols = cols;
                prev_count = 0; // force all lines as different
        }

        int max_count = (curr_count > prev_count) ? curr_count : prev_count;

        for (int i = 0; i < max_count; i++) {
                const char *prev = (i < prev_count) ? prev_lines[i] : "";
                const char *curr = (i < curr_count) ? curr_lines[i] : "";

                if (repaint_all || strcmp(prev, curr) != 0) {
                        ui_move(/*row*/ i + 1, /*col*/ 1);
                        fputs(curr, stdout);
                        ui_clear_eol();
                }
        }

        // @@ Do not clear the output region: the screen compositor owns it.
        // The loop above already clears rows left behind when the banner shrinks.
        fflush(stdout);

        // Store current as previous for next diff
        prev_count = curr_count;
        for (int i = 0; i < curr_count; i++)
                snprintf(prev_lines[i], MAX_LINE_COLS, "%s", curr_lines[i]);
        for (int i = curr_count; i < MAX_BANNER_LINES; i++)
                prev_lines[i][0] = '\0';

        return curr_count;
}
