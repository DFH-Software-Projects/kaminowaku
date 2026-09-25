// Copyright 2026 Jamison A. Drapeau
// @@ KUI physical-screen compositor. One terminal writer owns this state.
#ifndef __UI_SCREEN__H
#define __UI_SCREEN__H

#include <stddef.h>

#define UI_SCREEN_MAX_ROWS 4096
#define UI_SCREEN_MAX_ROW_BYTES 2048
#define UI_SCREEN_MAX_STYLE_BYTES 1024

typedef struct {
        char text[UI_SCREEN_MAX_ROW_BYTES];
        char style[UI_SCREEN_MAX_STYLE_BYTES];
        size_t text_len;
        size_t style_len;
        int valid;
} UI_SCREEN_ROW;

typedef struct {
        UI_SCREEN_ROW *current;
        UI_SCREEN_ROW *desired;
        unsigned int rows;
        unsigned int cols;
        unsigned int top;
        unsigned int bottom;
        unsigned int dirty_rows;
        unsigned long long bytes_written;
        unsigned long long write_calls;
        int fd;
} UI_SCREEN;

int ui_screen_init(UI_SCREEN *screen, int fd);
void ui_screen_destroy(UI_SCREEN *screen);
void ui_screen_invalidate(UI_SCREEN *screen);
int ui_screen_begin(UI_SCREEN *screen, unsigned int rows, unsigned int cols,
        unsigned int top, unsigned int bottom);
int ui_screen_set(UI_SCREEN *screen, unsigned int row, const char *text,
        size_t text_len, const char *style, size_t style_len);
int ui_screen_commit(UI_SCREEN *screen);

#endif
