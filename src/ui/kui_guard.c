// Copyright 2026 Jamison A. Drapeau
#include "kui.h"

#undef kui_add_line_and_render

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int8_t KUI_PROCESSING_ACTIVE = ISFALSE;
static int8_t KUI_PROGRESS_ACTIVE = ISFALSE;
static uint64_t KUI_PROGRESS_TOTAL = 0;
static uint64_t KUI_PROGRESS_STARTED = 0;

// @@ Preserve existing processing behavior without racing renderer output.
static void kui_guard_mouse_disable(void) {
        kui_mouse_capture_set(ISFALSE);
}

static void kui_guard_mouse_enable(void) {
        kui_mouse_capture_set(ISTRUE);
}

void kui_processing_begin(void) {
        KUI_PROCESSING_ACTIVE = ISTRUE;
        kui_guard_mouse_disable();
}

void kui_processing_end(void) {
        KUI_PROCESSING_ACTIVE = ISFALSE;
        kui_guard_mouse_enable();
}

void kui_progress_begin(_carry_forward * _prog_data) {
        FLOWER * NODE;
        uint64_t TOTAL = 0;

        if (!_prog_data || !_prog_data->active_project_flower) {
                return;
        }

        NODE = _prog_data->active_project_flower->NEXT;
        while (NODE) {
                TOTAL++;
                NODE = NODE->NEXT;
        }

        if (TOTAL == 0) {
                return;
        }

        KUI_PROGRESS_TOTAL = TOTAL;
        KUI_PROGRESS_STARTED = 0;
        KUI_PROGRESS_ACTIVE = ISTRUE;

        kui_set_churning(
                NOTICE_CHURNING
                "%" PRIu64 "/%" PRIu64 " Targets waiting...",
                KUI_PROGRESS_TOTAL,
                KUI_PROGRESS_TOTAL
        );
        kui_render_page();

        if (KUI_PROCESSING_ACTIVE == ISTRUE) {
                kui_guard_mouse_disable();
        }
}

void kui_progress_advance(void) {
        if (KUI_PROGRESS_ACTIVE != ISTRUE) {
                return;
        }

        if (KUI_PROGRESS_STARTED < KUI_PROGRESS_TOTAL) {
                KUI_PROGRESS_STARTED++;
        }

        kui_set_churning(
                NOTICE_CHURNING
                "%" PRIu64 "/%" PRIu64 " Targets waiting...",
                KUI_PROGRESS_TOTAL - KUI_PROGRESS_STARTED,
                KUI_PROGRESS_TOTAL
        );
}

void kui_progress_end(void) {
        if (KUI_PROGRESS_ACTIVE != ISTRUE) {
                return;
        }

        KUI_PROGRESS_ACTIVE = ISFALSE;
        KUI_PROGRESS_TOTAL = 0;
        KUI_PROGRESS_STARTED = 0;
        kui_clear_churning();
        kui_render_page();

        if (KUI_PROCESSING_ACTIVE == ISTRUE) {
                kui_guard_mouse_disable();
        }
}

void kui_add_line_and_render_guard(const char *fmt, ...) {
        char BUFFER[KUI_MAX_SCROLL_COLS];
        va_list AP;

        if (!fmt) {
                return;
        }

        memset(BUFFER, 0x00, sizeof(BUFFER));
        va_start(AP, fmt);
        (void)vsnprintf(BUFFER, sizeof(BUFFER), fmt, AP);
        va_end(AP);

        kui_add_line_and_render("%s", BUFFER);

        if (KUI_PROCESSING_ACTIVE == ISTRUE) {
                kui_guard_mouse_disable();
        }
}
