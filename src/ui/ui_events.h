// Copyright 2026 Jamison A. Drapeau
// @@ KUI ordered event transport; synchronized for future renderer/worker threads.
#ifndef __UI_EVENTS__H
#define __UI_EVENTS__H

#include "data.h"

// @@ Do not pass mutable command buffers across the UI boundary.
// @@ Events are copied into bounded transport storage, never borrowed.
#define UI_EVENT_CAPACITY 64

typedef enum {
        UI_EVENT_OUTPUT = 0,
        UI_EVENT_SCROLL,
        UI_EVENT_INPUT,
        UI_EVENT_RENDER,
        UI_EVENT_INPUT_BEGIN,
        UI_EVENT_INPUT_END,
        UI_EVENT_BELL,
        UI_EVENT_FRAME_START,
        UI_EVENT_CLEAR,
        UI_EVENT_FLUSH,
        UI_EVENT_CHURNING,
        UI_EVENT_CHURNING_CLEAR,
        UI_EVENT_MODE,
        UI_EVENT_MODE_GET,
        UI_EVENT_MOUSE,
        UI_EVENT_STOP
} ui_event_type_t;

typedef struct {
        ui_event_type_t type;
        int scroll_rows;
        unsigned int cursor;
        int value;
        void *completion; // @@ Optional synchronous barrier, owned by caller until acknowledgment.
        char output[KUI_MAX_SCROLL_COLS];
        char input[INPUT_BLOCK];
        char prompt[DOUBLE_BLOCK];
} ui_event_t;

void ui_events_reset(void);
int ui_events_post(const ui_event_t *event);
int ui_events_next(ui_event_t *event);
int ui_events_post_wait(const ui_event_t *event);
int ui_events_wait_next(ui_event_t *event);
void ui_events_close(void);
unsigned int ui_events_pending(void);

#endif
