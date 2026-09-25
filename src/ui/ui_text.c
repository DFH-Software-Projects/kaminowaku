// Copyright 2026 Jamison A. Drapeau
// @@ Convert scrollback spans into terminal-safe physical rows.
#define _XOPEN_SOURCE 700
#include "ui_text.h"

#include <string.h>
#include <wchar.h>

#define UI_TEXT_TAB_STOP 8U

void ui_text_style_reset(UI_TEXT_STYLE *state) {
        if (!state) return;
        state->length = 0;
        state->sgr[0] = '\0';
}

/* @@ Replay only complete SGR escapes. Keep the original ordering so
 * foreground, background, bold, and 24-bit colors combine as emitted.
 * Each physical row starts with an explicit reset, then this replay.
 */
void ui_text_style_feed(UI_TEXT_STYLE *state, const char *source, size_t length) {
        size_t i = 0;
        if (!state || !source) return;
        while (i < length) {
                size_t start, end;
                int valid = 1;
                if ((unsigned char)source[i++] != 0x1b) continue;
                start = i - 1;
                if (i >= length) break;
                if (source[i] == ']') {
                        /* OSC is unrelated to SGR. Skip embedded CSI-like text. */
                        i++;
                        while (i < length) {
                                if (source[i++] == '\a') break;
                                if (source[i - 1] == '\x1b'
                                                && i < length && source[i] == '\\') {
                                        i++;
                                        break;
                                }
                        }
                        continue;
                }
                if (source[i] != '[') {
                        i++;
                        continue;
                }
                i++;
                while (i < length && (unsigned char)source[i] >= 0x20
                                && (unsigned char)source[i] <= 0x3f) {
                        unsigned char c = (unsigned char)source[i++];
                        if (!((c >= '0' && c <= '9') || c == ';' || c == ':'))
                                valid = 0;
                }
                if (i >= length) break; /* Truncated CSI. */
                if ((unsigned char)source[i] < 0x40
                                || (unsigned char)source[i] > 0x7e) continue;
                if (source[i++] != 'm' || !valid) continue;
                end = i;
                /* A leading SGR reset discards all earlier styling.
                 * Do not confuse zero RGB components with a reset. */
                size_t param = start + 2;
                if (param + 1 == end
                                || (param < end && source[param] == '0'
                                                && (param + 1 == end - 1
                                                        || source[param + 1] == ';'
                                                        || source[param + 1] == ':'))) {
                        state->length = 0;
                        state->sgr[0] = '\0';
                        /* ESC[m and ESC[0m are complete resets, no replay needed. */
                        if (param + 1 == end
                                        || (source[param] == '0' && param + 2 == end))
                                continue;
                }
                if (end - start >= sizeof(state->sgr)) continue;
                if (state->length + (end - start) >= sizeof(state->sgr)) {
                        /* Bound replay length even for streams with no reset.
                         * Prefer the newest SGR when the previous history overflows. */
                        state->length = 0;
                }
                memcpy(state->sgr + state->length, source + start, end - start);
                state->length += end - start;
                state->sgr[state->length] = '\0';
        }
}

unsigned int ui_text_tab_width(unsigned int column) {
        return UI_TEXT_TAB_STOP - column % UI_TEXT_TAB_STOP;
}

// @@ Only SGR is permitted through the output viewport. Movement, erasure,
// OSC/title controls and C0 characters must never escape their assigned row.
size_t ui_text_render_row(const char *source, size_t length,
        char *output, size_t capacity) {
        size_t i = 0, used = 0;
        unsigned int column = 0;
        if (!output || !capacity) return 0;
        output[0] = '\0';
        if (!source) return 0;
        while (i < length) {
                unsigned char ch = (unsigned char)source[i];
                if (ch == 0x1b) {
                        size_t begin = i++;
                        if (i < length && source[i] == '[') {
                                int sgr = 1;
                                i++;
                                while (i < length && (unsigned char)source[i] >= 0x20
                                                && (unsigned char)source[i] <= 0x3f) {
                                        unsigned char param = (unsigned char)source[i++];
                                        if (!((param >= '0' && param <= '9')
                                                        || param == ';' || param == ':')) sgr = 0;
                                }
                                if (i < length && (unsigned char)source[i] >= 0x40
                                                && (unsigned char)source[i] <= 0x7e) {
                                        if (source[i++] == 'm' && sgr && i - begin < capacity - used) {
                                                memcpy(output + used, source + begin, i - begin);
                                                used += i - begin;
                                        }
                                } else {
                                        // @@ Truncated CSI: never print its unfinished tail.
                                        i = length;
                                }
                        } else if (i < length && source[i] == ']') {
                                // @@ Ignore entire OSC, including hyperlinks and title changes.
                                i++;
                                while (i < length) {
                                        if (source[i++] == '\a') break;
                                        if (source[i - 1] == '\x1b' && i < length && source[i] == '\\') {
                                                i++;
                                                break;
                                        }
                                }
                        } else if (i < length) {
                                i++; // @@ Unknown ESC command: discard its introducer.
                        }
                        continue;
                }
                if (ch == '\t') {
                        unsigned int spaces = ui_text_tab_width(column);
                        while (spaces-- && used + 1 < capacity) {
                                output[used++] = ' ';
                                column++;
                        }
                        i++;
                        continue;
                }
                if (ch < 0x20 || ch == 0x7f) {
                        i++;
                        continue;
                }
                mbstate_t state = {0};
                wchar_t glyph;
                size_t bytes = mbrtowc(&glyph, source + i, length - i, &state);
                int width;
                if (bytes == (size_t)-1 || bytes == (size_t)-2 || bytes == 0) {
                        bytes = 1;
                        width = 1;
                } else {
                        width = wcwidth(glyph);
                        if (width < 0) width = 1;
                }
                if (bytes >= capacity - used) break;
                memcpy(output + used, source + i, bytes);
                used += bytes;
                column += (unsigned int)width;
                i += bytes;
        }
        output[used] = '\0';
        return used;
}
