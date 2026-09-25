// Copyright 2026 Jamison A. Drapeau
// @@ KUI event transport - synchronous in Beta V2 Phase 1.
#ifndef __UI_EVENTS__H
#define __UI_EVENTS__H

#include "data.h"

// @@ Do not pass mutable command buffers across the UI boundary.
// Phase 4 will add synchronization; producers and consumer share one thread today.
#define UI_EVENT_CAPACITY 64

typedef enum {
        UI_EVENT_OUTPUT = 0,
        UI_EVENT_SCROLL,
        UI_EVENT_INPUT
} ui_event_type_t;

typedef struct {
        ui_event_type_t type;
        int scroll_rows;
        unsigned int cursor;
        char output[KUI_MAX_SCROLL_COLS];
        char input[INPUT_BLOCK];
        char prompt[DOUBLE_BLOCK];
} ui_event_t;

void ui_events_reset(void);
int ui_events_post(const ui_event_t *event);
int ui_events_next(ui_event_t *event);

#endif
