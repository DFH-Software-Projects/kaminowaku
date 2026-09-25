// Copyright 2026 Jamison A. Drapeau
// @@ Beta V2 Phase 1 regression: ordered, snapshot-owning synchronous UI queue.
#include "ui_events.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
        ui_event_t event = {0};
        ui_event_t received = {0};
        char expected[64];

        ui_events_reset();
        assert(ui_events_next(&received) == 0);
        assert(ui_events_post(NULL) == -1);

        for (int i = 0; i < UI_EVENT_CAPACITY; i++) {
                event.type = UI_EVENT_OUTPUT;
                snprintf(event.output, sizeof(event.output), "event-%d", i);
                assert(ui_events_post(&event) == 0);
        }

        assert(ui_events_post(&event) == -1);

        for (int i = 0; i < 17; i++) {
                assert(ui_events_next(&received) == 1);
                snprintf(expected, sizeof(expected), "event-%d", i);
                assert(strcmp(received.output, expected) == 0);
        }

        // @@ Exercise head/tail wrapping without losing FIFO order.
        for (int i = UI_EVENT_CAPACITY; i < UI_EVENT_CAPACITY + 17; i++) {
                snprintf(event.output, sizeof(event.output), "event-%d", i);
                assert(ui_events_post(&event) == 0);
        }

        for (int i = 17; i < UI_EVENT_CAPACITY + 17; i++) {
                assert(ui_events_next(&received) == 1);
                snprintf(expected, sizeof(expected), "event-%d", i);
                assert(strcmp(received.output, expected) == 0);
        }

        assert(ui_events_next(&received) == 0);

        // @@ Mutating a producer buffer must not mutate the posted UI snapshot.
        event.type = UI_EVENT_INPUT;
        snprintf(event.input, sizeof(event.input), "%s", "first");
        snprintf(event.prompt, sizeof(event.prompt), "%s", "[target]> ");
        event.cursor = 4;
        assert(ui_events_post(&event) == 0);
        snprintf(event.input, sizeof(event.input), "%s", "mutated");
        assert(ui_events_next(&received) == 1);
        assert(strcmp(received.input, "first") == 0);
        assert(received.cursor == 4);

        event.type = UI_EVENT_SCROLL;
        event.scroll_rows = -3;
        assert(ui_events_post(&event) == 0);
        ui_events_reset();
        assert(ui_events_next(&received) == 0);

        puts("PASS: ordered UI queue, wraparound, capacity, immutable snapshots, reset");
        return 0;
}
