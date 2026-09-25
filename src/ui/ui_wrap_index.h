// Copyright 2026 Jamison A. Drapeau
// @@ Width-specific index of physical rows in logical scrollback.
#ifndef __UI_WRAP_INDEX__H
#define __UI_WRAP_INDEX__H

#include "data.h"

typedef unsigned int (*ui_wrap_rows_fn)(const char *line, unsigned int cols);

typedef struct {
        unsigned int cols;
        unsigned int head;
        unsigned int line_count;
        unsigned int total_rows;
        unsigned int rows[KUI_MAX_SCROLL_LINES];
        unsigned int prefix[KUI_MAX_SCROLL_LINES + 1];
} UI_WRAP_INDEX;

void ui_wrap_index_reset(UI_WRAP_INDEX *index);
void ui_wrap_index_sync(UI_WRAP_INDEX *index, const KUI_SCROLLBACK *sb,
        unsigned int cols, ui_wrap_rows_fn count_rows);
int ui_wrap_index_locate(const UI_WRAP_INDEX *index, unsigned int physical_row,
        unsigned int *logical_line, unsigned int *inside_line);

#endif
