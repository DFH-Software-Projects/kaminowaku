// Copyright 2026 Jamison A. Drapeau
// @@ Fixed-capacity ordered KUI event queue.
#include "ui_events.h"

#include <string.h>

static ui_event_t UI_EVENTS[UI_EVENT_CAPACITY];
static unsigned int UI_EVENT_HEAD = 0;
static unsigned int UI_EVENT_COUNT = 0;

void ui_events_reset(void) {
        UI_EVENT_HEAD = 0;
        UI_EVENT_COUNT = 0;
}

int ui_events_post(const ui_event_t *event) {
        unsigned int tail;
        if (!event || UI_EVENT_COUNT >= UI_EVENT_CAPACITY) return -1;

        tail = (UI_EVENT_HEAD + UI_EVENT_COUNT) % UI_EVENT_CAPACITY;
        UI_EVENTS[tail] = *event;
        UI_EVENT_COUNT++;
        return 0;
}

int ui_events_next(ui_event_t *event) {
        if (!event || UI_EVENT_COUNT == 0) return 0;

        *event = UI_EVENTS[UI_EVENT_HEAD];
        UI_EVENT_HEAD = (UI_EVENT_HEAD + 1) % UI_EVENT_CAPACITY;
        UI_EVENT_COUNT--;
        return 1;
}
