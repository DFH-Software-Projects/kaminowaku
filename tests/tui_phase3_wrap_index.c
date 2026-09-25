// Copyright 2026 Jamison A. Drapeau
// @@ Test physical-row indexing independently of the terminal.
#include "ui_wrap_index.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned int count_rows(const char *s, unsigned int cols) {
        unsigned int n = (unsigned int)strlen(s);
        return (n + cols - 1) / cols;
}

int main(void) {
        KUI_SCROLLBACK *sb = calloc(1, sizeof(*sb));
        UI_WRAP_INDEX *index = calloc(1, sizeof(*index));
        unsigned int logical, inside;
        assert(sb && index);

        strcpy(sb->lines[0], "ab");
        strcpy(sb->lines[1], "1234567");
        sb->line_count = 2;
        ui_wrap_index_sync(index, sb, 3, count_rows);
        assert(index->total_rows == 4);
        assert(ui_wrap_index_locate(index, 3, &logical, &inside) == 0);
        assert(logical == 1 && inside == 2);

        strcpy(sb->lines[2], "abc");
        sb->line_count = 3;
        ui_wrap_index_sync(index, sb, 3, count_rows);
        assert(index->total_rows == 5);
        ui_wrap_index_sync(index, sb, 7, count_rows);
        assert(index->total_rows == 3);

        for (unsigned int i = 3; i < KUI_MAX_SCROLL_LINES; i++)
                strcpy(sb->lines[i], "A");
        sb->line_count = KUI_MAX_SCROLL_LINES;
        ui_wrap_index_sync(index, sb, 7, count_rows);
        assert(index->total_rows == KUI_MAX_SCROLL_LINES);

        sb->head = 1;
        strcpy(sb->lines[0], "12345678");
        ui_wrap_index_sync(index, sb, 7, count_rows);
        assert(index->total_rows == KUI_MAX_SCROLL_LINES + 1);
        assert(ui_wrap_index_locate(index, index->total_rows - 1,
                &logical, &inside) == 0);
        assert(logical == KUI_MAX_SCROLL_LINES - 1 && inside == 1);

        free(index);
        free(sb);
        puts("PASS: append, resize, full-ring eviction, binary-search row lookup");
        return 0;
}
