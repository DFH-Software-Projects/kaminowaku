// Copyright 2026 Jamison A. Drapeau
#ifndef __UI_LAYOUT__H
#define __UI_LAYOUT__H
static inline unsigned int ui_prompt_row(unsigned int content_top, unsigned int rendered_rows, unsigned int terminal_rows, int churning) {
    if (!terminal_rows) return 0;
    unsigned int row = content_top + rendered_rows;
    if (row > terminal_rows) row = terminal_rows;
    if (churning && row >= terminal_rows - 1) row = terminal_rows;
    return row;
}
#endif
