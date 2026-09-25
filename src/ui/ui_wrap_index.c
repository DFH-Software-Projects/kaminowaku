// Copyright 2026 Jamison A. Drapeau
// @@ Cached wrap counts; binary-search viewport positioning.
#include "ui_wrap_index.h"

#include <string.h>

void ui_wrap_index_reset(UI_WRAP_INDEX *index) {
        if (index) memset(index, 0, sizeof(*index));
}

static unsigned int ui_wrap_line(const KUI_SCROLLBACK *sb, unsigned int logical,
        unsigned int cols, ui_wrap_rows_fn count_rows) {
        unsigned int physical = (sb->head + logical) % KUI_MAX_SCROLL_LINES;
        unsigned int rows = count_rows(sb->lines[physical], cols);
        return rows > 0 ? rows : 1;
}

void ui_wrap_index_sync(UI_WRAP_INDEX *index, const KUI_SCROLLBACK *sb,
        unsigned int cols, ui_wrap_rows_fn count_rows) {
        unsigned int i;
        if (!index || !sb || !count_rows || cols == 0) return;
        if (sb->line_count > KUI_MAX_SCROLL_LINES) return;
        if (index->cols == cols && index->head == sb->head
                && index->line_count == sb->line_count) return;

        // @@ The common append path wraps only newly produced logical lines.
        if (index->cols == cols && index->head == sb->head
                && sb->line_count > index->line_count) {
                for (i = index->line_count; i < sb->line_count; i++) {
                        index->rows[i] = ui_wrap_line(sb, i, cols, count_rows);
                        index->prefix[i + 1] = index->prefix[i] + index->rows[i];
                }
                index->line_count = sb->line_count;
                index->total_rows = index->prefix[index->line_count];
                return;
        }

        // @@ When a full ring evicts one line, shift cached counts, not text.
        if (index->cols == cols && index->line_count == KUI_MAX_SCROLL_LINES
                && sb->line_count == KUI_MAX_SCROLL_LINES
                && sb->head == (index->head + 1) % KUI_MAX_SCROLL_LINES) {
                memmove(index->rows, index->rows + 1,
                        (KUI_MAX_SCROLL_LINES - 1) * sizeof(index->rows[0]));
                index->rows[KUI_MAX_SCROLL_LINES - 1] = ui_wrap_line(sb,
                        KUI_MAX_SCROLL_LINES - 1, cols, count_rows);
        } else {
                // @@ Width changes or multiple unseen evictions require a rebuild.
                for (i = 0; i < sb->line_count; i++)
                        index->rows[i] = ui_wrap_line(sb, i, cols, count_rows);
        }
        index->prefix[0] = 0;
        for (i = 0; i < sb->line_count; i++)
                index->prefix[i + 1] = index->prefix[i] + index->rows[i];
        index->cols = cols;
        index->head = sb->head;
        index->line_count = sb->line_count;
        index->total_rows = index->prefix[index->line_count];
}

int ui_wrap_index_locate(const UI_WRAP_INDEX *index, unsigned int physical_row,
        unsigned int *logical_line, unsigned int *inside_line) {
        unsigned int low, high, mid;
        if (!index || !logical_line || !inside_line
                || physical_row >= index->total_rows) return -1;
        low = 0;
        high = index->line_count;
        while (low < high) {
                mid = low + (high - low) / 2;
                if (index->prefix[mid + 1] <= physical_row) low = mid + 1;
                else high = mid;
        }
        *logical_line = low;
        *inside_line = physical_row - index->prefix[low];
        return 0;
}
