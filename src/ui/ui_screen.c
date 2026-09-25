// Copyright 2026 Jamison A. Drapeau
// @@ Screen diffing is independent of scrollback and command execution.
#include "ui_screen.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int ui_screen_write(int fd, const char *buffer, size_t length) {
        while (length > 0) {
                ssize_t n = write(fd, buffer, length);
                if (n < 0 && errno == EINTR) continue;
                if (n <= 0) return -1;
                buffer += (size_t)n;
                length -= (size_t)n;
        }
        return 0;
}

int ui_screen_init(UI_SCREEN *screen, int fd) {
        if (!screen || fd < 0) return -1;
        memset(screen, 0, sizeof(*screen));
        screen->fd = fd;
        return 0;
}

void ui_screen_destroy(UI_SCREEN *screen) {
        if (!screen) return;
        free(screen->current);
        free(screen->desired);
        memset(screen, 0, sizeof(*screen));
}

void ui_screen_invalidate(UI_SCREEN *screen) {
        if (!screen || !screen->current) return;
        for (unsigned int i = 0; i < screen->rows; i++)
                screen->current[i].valid = 0;
}

/* @@ Out-of-band prompt edits must invalidate their former physical row. */
void ui_screen_invalidate_row(UI_SCREEN *screen, unsigned int row) {
        if (screen && screen->current && row > 0 && row <= screen->rows)
                screen->current[row - 1].valid = 0;
}

int ui_screen_begin(UI_SCREEN *screen, unsigned int rows, unsigned int cols,
        unsigned int top, unsigned int bottom) {
        UI_SCREEN_ROW *current;
        UI_SCREEN_ROW *desired;
        int changed;

        if (!screen || rows == 0 || rows > UI_SCREEN_MAX_ROWS || cols == 0
                || top == 0 || bottom < top || bottom > rows) return -1;
        changed = screen->rows != rows || screen->cols != cols
                || screen->top != top || screen->bottom != bottom;
        if (screen->rows != rows) {
                current = calloc(rows, sizeof(*current));
                desired = calloc(rows, sizeof(*desired));
                if (!current || !desired) {
                        free(current);
                        free(desired);
                        return -1;
                }
                free(screen->current);
                free(screen->desired);
                screen->current = current;
                screen->desired = desired;
        }
        screen->rows = rows;
        screen->cols = cols;
        screen->top = top;
        screen->bottom = bottom;
        screen->dirty_rows = 0;
        if (changed) ui_screen_invalidate(screen);
        // @@ All desired rows start empty; the scrollback renderer fills visible ones.
        for (unsigned int i = top - 1; i < bottom; i++) {
                screen->desired[i].text_len = 0;
                screen->desired[i].style_len = 0;
                screen->desired[i].text[0] = '\0';
                screen->desired[i].style[0] = '\0';
                screen->desired[i].valid = 1;
        }
        return 0;
}

int ui_screen_set(UI_SCREEN *screen, unsigned int row, const char *text,
        size_t text_len, const char *style, size_t style_len) {
        UI_SCREEN_ROW *target;
        if (!screen || !screen->desired || row < screen->top || row > screen->bottom
                || (text_len && !text) || (style_len && !style)
                || text_len >= UI_SCREEN_MAX_ROW_BYTES
                || style_len >= UI_SCREEN_MAX_STYLE_BYTES) return -1;
        target = &screen->desired[row - 1];
        if (text_len) memcpy(target->text, text, text_len);
        if (style_len) memcpy(target->style, style, style_len);
        target->text[text_len] = '\0';
        target->style[style_len] = '\0';
        target->text_len = text_len;
        target->style_len = style_len;
        target->valid = 1;
        return 0;
}

int ui_screen_commit(UI_SCREEN *screen) {
        char buffer[UI_SCREEN_MAX_ROW_BYTES + UI_SCREEN_MAX_STYLE_BYTES + 96];
        if (!screen || !screen->current || !screen->desired) return -1;
        screen->dirty_rows = 0;
        for (unsigned int i = screen->top - 1; i < screen->bottom; i++) {
                UI_SCREEN_ROW *old = &screen->current[i];
                UI_SCREEN_ROW *next = &screen->desired[i];
                size_t used;
                int count;
                if (old->valid && old->text_len == next->text_len
                        && old->style_len == next->style_len
                        && memcmp(old->text, next->text, next->text_len) == 0
                        && memcmp(old->style, next->style, next->style_len) == 0)
                        continue;
                count = snprintf(buffer, sizeof(buffer), "\x1b[%u;1H\x1b[0m", i + 1);
                if (count < 0 || (size_t)count >= sizeof(buffer)) return -1;
                used = (size_t)count;
                if (used + next->style_len + next->text_len + 7 > sizeof(buffer)) return -1;
                memcpy(buffer + used, next->style, next->style_len);
                used += next->style_len;
                memcpy(buffer + used, next->text, next->text_len);
                used += next->text_len;
                memcpy(buffer + used, "\x1b[0m\x1b[K", 7);
                used += 7;
                if (ui_screen_write(screen->fd, buffer, used) != 0) return -1;
                screen->bytes_written += (unsigned long long)used;
                screen->write_calls++;
                *old = *next;
                screen->dirty_rows++;
        }
        return 0;
}
