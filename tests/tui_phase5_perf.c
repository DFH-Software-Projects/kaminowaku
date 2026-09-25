// Copyright 2026 Jamison A. Drapeau
// @@ Benchmark the compositor and maximum configured scrollback without TTY noise.
#define _POSIX_C_SOURCE 200809L
#include "ui_screen.h"
#include "ui_wrap_index.h"

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define PERF_FRAMES 1000

static long elapsed_us(struct timespec start, struct timespec stop) {
        return (long)(stop.tv_sec - start.tv_sec) * 1000000L
                + (long)(stop.tv_nsec - start.tv_nsec) / 1000L;
}

static unsigned int wrap(const char *line, unsigned int cols) {
        size_t n = strlen(line);
        return n ? (unsigned int)((n + cols - 1) / cols) : 1;
}

int main(void) {
        int sink = open("/dev/null", O_WRONLY);
        assert(sink >= 0);
        UI_SCREEN *screen = calloc(1, sizeof(*screen));
        assert(screen && ui_screen_init(screen, sink) == 0);
        assert(ui_screen_begin(screen, 48, 120, 4, 47) == 0);
        assert(ui_screen_set(screen, 4, "static", 6, "", 0) == 0);
        assert(ui_screen_commit(screen) == 0 && screen->dirty_rows == 44);

        struct timespec start, stop;
        assert(clock_gettime(CLOCK_MONOTONIC, &start) == 0);
        for (int i = 0; i < PERF_FRAMES; i++) {
                assert(ui_screen_begin(screen, 48, 120, 4, 47) == 0);
                assert(ui_screen_set(screen, 4, "static", 6, "", 0) == 0);
                assert(ui_screen_commit(screen) == 0);
                assert(screen->dirty_rows == 0);
        }
        assert(clock_gettime(CLOCK_MONOTONIC, &stop) == 0);
        printf("Idle virtual frames: %d; dirty rows: 0; elapsed: %ld us\n",
                PERF_FRAMES, elapsed_us(start, stop));

        assert(clock_gettime(CLOCK_MONOTONIC, &start) == 0);
        for (int i = 0; i < PERF_FRAMES; i++) {
                char text[64];
                int n = snprintf(text, sizeof(text), "changing-%d", i);
                assert(n > 0);
                assert(ui_screen_begin(screen, 48, 120, 4, 47) == 0);
                assert(ui_screen_set(screen, 4, text, (size_t)n, "", 0) == 0);
                assert(ui_screen_commit(screen) == 0);
                assert(screen->dirty_rows == 1);
        }
        assert(clock_gettime(CLOCK_MONOTONIC, &stop) == 0);
        printf("Single-row virtual frames: %d; elapsed: %ld us\n",
                PERF_FRAMES, elapsed_us(start, stop));
        ui_screen_destroy(screen);
        close(sink);
        free(screen);

        KUI_SCROLLBACK *sb = calloc(1, sizeof(*sb));
        UI_WRAP_INDEX *idx = calloc(1, sizeof(*idx));
        assert(sb && idx);
        for (unsigned int i = 0; i < KUI_MAX_SCROLL_LINES; i++)
                strcpy(sb->lines[i], "An example line with a few words and wider than 20.");
        sb->line_count = KUI_MAX_SCROLL_LINES;

        assert(clock_gettime(CLOCK_MONOTONIC, &start) == 0);
        ui_wrap_index_sync(idx, sb, 20, wrap);
        for (int i = 0; i < PERF_FRAMES; i++)
                ui_wrap_index_sync(idx, sb, 20, wrap);
        assert(clock_gettime(CLOCK_MONOTONIC, &stop) == 0);
        printf("Cached index checks: %d over %u lines; elapsed: %ld us\n",
                PERF_FRAMES, sb->line_count, elapsed_us(start, stop));
        assert(idx->total_rows == sb->line_count * 3);

        assert(clock_gettime(CLOCK_MONOTONIC, &start) == 0);
        for (int i = 0; i < PERF_FRAMES; i++) {
                unsigned int old_head = sb->head;
                sb->head = (sb->head + 1) % KUI_MAX_SCROLL_LINES;
                strcpy(sb->lines[old_head], "short");
                ui_wrap_index_sync(idx, sb, 20, wrap);
        }
        assert(clock_gettime(CLOCK_MONOTONIC, &stop) == 0);
        printf("Ring evictions: %d; elapsed: %ld us\n",
                PERF_FRAMES, elapsed_us(start, stop));
        free(idx);
        free(sb);
        puts("PASS: zero unchanged paints, single-row diff and maximum scrollback indexing");
        return 0;
}
