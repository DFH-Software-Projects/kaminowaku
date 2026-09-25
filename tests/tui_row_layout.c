// Copyright 2026 Jamison A. Drapeau
// @@ Prompt placement, tab stops, and control-free physical rows.
#include "ui_layout.h"
#include "ui_text.h"
#include "ui_screen.h"

#include <assert.h>
#include <locale.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(void) {
        char row[512];
        int fd[2];
        UI_SCREEN screen;
        (void)setlocale(LC_CTYPE, "");

        assert(ui_prompt_row(7, 0, 30, 0) == 7);
        assert(ui_prompt_row(7, 3, 30, 0) == 10);
        assert(ui_prompt_row(7, 23, 30, 0) == 30);
        assert(ui_prompt_row(7, 1, 30, 1) == 8);
        assert(ui_prompt_row(7, 22, 30, 1) == 30);
        assert(ui_text_tab_width(0) == 8 && ui_text_tab_width(1) == 7);

        const char *help = "\x1b[35m[\thelp\t\t\t<-- Prints this help menu\t\t] h\x1b[0m";
        size_t length = ui_text_render_row(help, strlen(help), row, sizeof(row));
        assert(length && !memchr(row, '\t', length));
        assert(strstr(row, "[       help") != NULL);
        assert(strstr(row, "Prints this help menu") != NULL);
        assert(strstr(row, "\x1b[35m") && strstr(row, "\x1b[0m"));

        const char *controls = "ok\x1b[10;10Hbad\x1b]2;overwrite\a!\r\nnext\x1b[31mred\x1b[0m";
        length = ui_text_render_row(controls, strlen(controls), row, sizeof(row));
        assert(!strstr(row, "[10;10H") && !strstr(row, "overwrite"));
        assert(!memchr(row, '\r', length) && !memchr(row, '\n', length));
        assert(strstr(row, "okbad!next\x1b[31mred\x1b[0m"));
        const char *box = "┏━━━━━━┓";
        length = ui_text_render_row(box, strlen(box), row, sizeof(row));
        assert(length == strlen(box) && strcmp(row, box) == 0);

        /* @@ Kami's COMMAND LIST box begins color on line one, resets it
         * around title text on line two, then carries it to line three. */
        UI_TEXT_STYLE colors;
        ui_text_style_reset(&colors);
        assert(colors.length == 0);
        const char *top = "\x1b[36m┏━━━━━━━━━━━━━━┓";
        ui_text_style_feed(&colors, top, strlen(top));
        assert(strcmp(colors.sgr, "\x1b[36m") == 0);
        const char *middle = "┃\x1b[0m COMMAND LIST \x1b[36m┃";
        ui_text_style_feed(&colors, middle, strlen(middle));
        assert(strcmp(colors.sgr, "\x1b[36m") == 0);
        const char *bottom = "┗━━━━━━━━━━━━━━┛\x1b[0m";
        ui_text_style_feed(&colors, bottom, strlen(bottom));
        assert(colors.length == 0);

        /* @@ Scrollback replays SGR through hidden logical rows. */
        ui_text_style_feed(&colors, "\x1b[1m\x1b[38;2;48;0;48m",
                        strlen("\x1b[1m\x1b[38;2;48;0;48m"));
        assert(strstr(colors.sgr, "\x1b[1m"));
        assert(strstr(colors.sgr, "\x1b[38;2;48;0;48m"));
        ui_text_style_feed(&colors, "\x1b]0;title \x1b[31m\a",
                        strlen("\x1b]0;title \x1b[31m\a"));
        assert(!strstr(colors.sgr, "\x1b[31m"));
        ui_text_style_feed(&colors, "\x1b[39m", strlen("\x1b[39m"));
        assert(strstr(colors.sgr, "\x1b[39m"));
        ui_text_style_feed(&colors, "\x1b[0;36m", strlen("\x1b[0;36m"));
        assert(strcmp(colors.sgr, "\x1b[0;36m") == 0);
        ui_text_style_feed(&colors, "\x1b[m", strlen("\x1b[m"));
        assert(colors.length == 0);

        assert(pipe(fd) == 0);
        assert(ui_screen_init(&screen, fd[1]) == 0);
        assert(ui_screen_begin(&screen, 30, 100, 7, 30) == 0);
        assert(ui_screen_set(&screen, 7, "output", 6, "", 0) == 0);
        assert(ui_screen_commit(&screen) == 0);
        assert(screen.dirty_rows == 24);
        // @@ Prompt is painted out-of-band; force its former physical row
        // to be redrawn even when the cached desired row is still blank.
        assert(ui_screen_begin(&screen, 30, 100, 7, 30) == 0);
        assert(ui_screen_set(&screen, 7, "output", 6, "", 0) == 0);
        assert(ui_screen_commit(&screen) == 0);
        assert(screen.dirty_rows == 0);
        ui_screen_invalidate_row(&screen, 30);
        assert(ui_screen_begin(&screen, 30, 100, 7, 30) == 0);
        assert(ui_screen_set(&screen, 7, "output", 6, "", 0) == 0);
        assert(ui_screen_commit(&screen) == 0);
        assert(screen.dirty_rows == 1);
        ui_screen_invalidate_row(&screen, 8);
        assert(ui_screen_begin(&screen, 30, 100, 7, 30) == 0);
        assert(ui_screen_set(&screen, 7, "output", 6, "", 0) == 0);
        assert(ui_screen_set(&screen, 8, "new output", 10, "", 0) == 0);
        assert(ui_screen_commit(&screen) == 0);
        assert(screen.dirty_rows == 1);
        ui_screen_destroy(&screen);
        close(fd[0]); close(fd[1]);
        puts("PASS: top-anchored prompt, full viewport, prompt ghost cleanup, ANSI/tab/UTF-8 rows");
        return 0;
}
