// Copyright 2026 Jamison A. Drapeau
// @@ ANSI-safe physical row rendering shared by KUI's scrollback compositor.
#ifndef __UI_TEXT__H
#define __UI_TEXT__H

#include <stddef.h>

/* Color state carried between independent logical and wrapped rows. */
#define UI_TEXT_STYLE_BYTES 1024
typedef struct {
        char sgr[UI_TEXT_STYLE_BYTES];
        size_t length;
} UI_TEXT_STYLE;

void ui_text_style_reset(UI_TEXT_STYLE *state);
void ui_text_style_feed(UI_TEXT_STYLE *state, const char *source, size_t length);
unsigned int ui_text_tab_width(unsigned int column);
size_t ui_text_render_row(const char *source, size_t length,
        char *output, size_t capacity);

#endif
