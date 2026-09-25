// Copyright 2026 Jamison A. Drapeau
// @@ ANSI-safe physical row rendering shared by KUI's scrollback compositor.
#ifndef __UI_TEXT__H
#define __UI_TEXT__H

#include <stddef.h>

unsigned int ui_text_tab_width(unsigned int column);
size_t ui_text_render_row(const char *source, size_t length,
        char *output, size_t capacity);

#endif
