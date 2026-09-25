// Copyright 2026 Jamison A. Drapeau
// @@ Phase 4 foundation: bounded cross-thread event delivery.
#include "ui_events.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_EVENTS 30000

static void *produce(void *argument) {
        (void)argument;
        ui_event_t event = {0};
        event.type = UI_EVENT_OUTPUT;
        for (int i = 0; i < TEST_EVENTS; i++) {
                snprintf(event.output, sizeof(event.output), "output-%d", i);
                assert(ui_events_post_wait(&event) == 0);
        }
        ui_events_close();
        return NULL;
}

static void *consume(void *argument) {
        (void)argument;
        ui_event_t event = {0};
        char expected[64];
        int count = 0;
        while (ui_events_wait_next(&event) == 1) {
                snprintf(expected, sizeof(expected), "output-%d", count);
                assert(event.type == UI_EVENT_OUTPUT);
                assert(strcmp(event.output, expected) == 0);
                count++;
        }
        assert(count == TEST_EVENTS);
        return NULL;
}

int main(void) {
        pthread_t producer;
        pthread_t consumer;
        ui_event_t event = {0};
        ui_events_reset();
        assert(pthread_create(&consumer, NULL, consume, NULL) == 0);
        assert(pthread_create(&producer, NULL, produce, NULL) == 0);
        assert(pthread_join(producer, NULL) == 0);
        assert(pthread_join(consumer, NULL) == 0);
        assert(ui_events_pending() == 0);
        assert(ui_events_post(&event) == -1);
        assert(ui_events_post_wait(&event) == -1);
        assert(ui_events_wait_next(&event) == 0);

        // @@ Reset reopens the queue for the next UI lifecycle.
        ui_events_reset();
        event.type = UI_EVENT_SCROLL;
        event.scroll_rows = 3;
        assert(ui_events_post(&event) == 0);
        assert(ui_events_next(&event) == 1);
        assert(event.scroll_rows == 3);
        ui_events_close();
        puts("PASS: 30000 ordered concurrent events, backpressure, drain and shutdown");
        return 0;
}
