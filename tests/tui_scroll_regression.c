// Copyright 2026 Jamison A. Drapeau
// @@ Wheel bursts coalesce without crossing keyboard/output ordering barriers.
#include "ui_events.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
        ui_event_t wheel = {0}, key = {0}, out = {0};
        ui_events_reset();
        wheel.type = UI_EVENT_SCROLL;
        wheel.scroll_rows = 3;
        for (unsigned i = 0; i < 1000; i++)
                assert(ui_events_post_wait(&wheel) == 0);
        assert(ui_events_pending() == 1);
        assert(ui_events_next(&out) == 1);
        assert(out.type == UI_EVENT_SCROLL && out.scroll_rows == 3000);

        // @@ A typed character must never be overtaken by subsequent scrolling.
        wheel.scroll_rows = 3;
        key.type = UI_EVENT_INPUT;
        strcpy(key.input, "typed");
        assert(ui_events_post(&wheel) == 0);
        assert(ui_events_post(&key) == 0);
        wheel.scroll_rows = -3;
        assert(ui_events_post(&wheel) == 0);
        assert(ui_events_pending() == 3);
        assert(ui_events_next(&out) == 1 && out.scroll_rows == 3);
        assert(ui_events_next(&out) == 1 && out.type == UI_EVENT_INPUT);
        assert(strcmp(out.input, "typed") == 0);
        assert(ui_events_next(&out) == 1 && out.scroll_rows == -3);

        // @@ Saturated queues still merge the final pending wheel event.
        ui_events_reset();
        key.type = UI_EVENT_INPUT;
        for (unsigned i = 0; i < UI_EVENT_CAPACITY - 1; i++)
                assert(ui_events_post(&key) == 0);
        wheel.scroll_rows = 1;
        assert(ui_events_post(&wheel) == 0);
        assert(ui_events_pending() == UI_EVENT_CAPACITY);
        assert(ui_events_post(&wheel) == 0);
        assert(ui_events_pending() == UI_EVENT_CAPACITY);
        for (unsigned i = 0; i < UI_EVENT_CAPACITY - 1; i++)
                assert(ui_events_next(&out) == 1 && out.type == UI_EVENT_INPUT);
        assert(ui_events_next(&out) == 1 && out.scroll_rows == 2);
        ui_events_close();
        puts("PASS: wheel coalescing, ordering barriers and saturated queue");
        return 0;
}
