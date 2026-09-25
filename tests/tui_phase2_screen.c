// Copyright 2026 Jamison A. Drapeau
// @@ Beta V2 Phase 2 regression: desired/current physical-screen diff.
#include "ui_screen.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(void) {
        int fds[2];
        UI_SCREEN screen;

        assert(pipe(fds) == 0);
        assert(ui_screen_init(&screen, fds[1]) == 0);
        assert(ui_screen_begin(&screen, 24, 80, 4, 22) == 0);
        assert(ui_screen_set(&screen, 4, "alpha", 5, "", 0) == 0);
        assert(ui_screen_commit(&screen) == 0);
        assert(screen.dirty_rows == 19);

        // @@ Identical desired/current rows must write nothing.
        assert(ui_screen_begin(&screen, 24, 80, 4, 22) == 0);
        assert(ui_screen_set(&screen, 4, "alpha", 5, "", 0) == 0);
        assert(ui_screen_commit(&screen) == 0);
        assert(screen.dirty_rows == 0);

        // @@ A single changed row paints without changing the others.
        assert(ui_screen_begin(&screen, 24, 80, 4, 22) == 0);
        assert(ui_screen_set(&screen, 4, "bravo", 5, "", 0) == 0);
        assert(ui_screen_commit(&screen) == 0);
        assert(screen.dirty_rows == 1);

        // @@ A removed line clears just its previous physical row.
        assert(ui_screen_begin(&screen, 24, 80, 4, 22) == 0);
        assert(ui_screen_commit(&screen) == 0);
        assert(screen.dirty_rows == 1);

        // @@ Reflow and explicit invalidation repaint all visible rows.
        assert(ui_screen_begin(&screen, 24, 100, 4, 22) == 0);
        assert(ui_screen_commit(&screen) == 0);
        assert(screen.dirty_rows == 19);
        assert(ui_screen_begin(&screen, 24, 100, 4, 22) == 0);
        assert(ui_screen_set(&screen, 4, "blue", 4, "\x1b[34m", 5) == 0);
        assert(ui_screen_commit(&screen) == 0);
        assert(screen.dirty_rows == 1);
        ui_screen_invalidate(&screen);
        assert(ui_screen_begin(&screen, 24, 100, 4, 22) == 0);
        assert(ui_screen_set(&screen, 4, "blue", 4, "\x1b[34m", 5) == 0);
        assert(ui_screen_commit(&screen) == 0);
        assert(screen.dirty_rows == 19);

        assert(ui_screen_set(&screen, 1, "outside", 7, "", 0) == -1);
        ui_screen_destroy(&screen);
        close(fds[0]);
        close(fds[1]);
        puts("PASS: virtual screen diff, stale rows, resize, ANSI state and invalidation");
        return 0;
}
