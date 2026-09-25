// Copyright 2026 Jamison A. Drapeau
// @@ Bounded, ordered UI queue shared by command and rendering threads.
#include "ui_events.h"

#include <pthread.h>
#include <stdint.h>
#include <limits.h>
#include <string.h>

static ui_event_t UI_EVENTS[UI_EVENT_CAPACITY];
static unsigned int UI_EVENT_HEAD = 0;
static unsigned int UI_EVENT_COUNT = 0;
static unsigned int UI_EVENT_PEAK = 0;
static int UI_EVENT_CLOSED = 0;
static pthread_mutex_t UI_EVENT_LOCK = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t UI_EVENT_AVAILABLE = PTHREAD_COND_INITIALIZER;
static pthread_cond_t UI_EVENT_SPACE = PTHREAD_COND_INITIALIZER;

// @@ Merge only adjacent unacknowledged wheel deltas. Never cross an input,
// output, resize or STOP event: those are ordering barriers.
static int ui_events_merge_scroll_locked(const ui_event_t *event) {
        if (!event || event->type != UI_EVENT_SCROLL || event->completion
                || UI_EVENT_COUNT == 0) return 0;
        unsigned int tail = (UI_EVENT_HEAD + UI_EVENT_COUNT - 1) % UI_EVENT_CAPACITY;
        ui_event_t *last = &UI_EVENTS[tail];
        if (last->type != UI_EVENT_SCROLL || last->completion) return 0;
        int64_t delta = (int64_t)last->scroll_rows + event->scroll_rows;
        if (delta > INT_MAX) delta = INT_MAX;
        if (delta < INT_MIN) delta = INT_MIN;
        last->scroll_rows = (int)delta;
        return 1;
}

static void ui_events_push_locked(const ui_event_t *event) {
        unsigned int tail = (UI_EVENT_HEAD + UI_EVENT_COUNT) % UI_EVENT_CAPACITY;
        UI_EVENTS[tail] = *event;
        UI_EVENT_COUNT++;
        if (UI_EVENT_COUNT > UI_EVENT_PEAK) UI_EVENT_PEAK = UI_EVENT_COUNT;
        pthread_cond_signal(&UI_EVENT_AVAILABLE);
}

static void ui_events_pop_locked(ui_event_t *event) {
        *event = UI_EVENTS[UI_EVENT_HEAD];
        UI_EVENT_HEAD = (UI_EVENT_HEAD + 1) % UI_EVENT_CAPACITY;
        UI_EVENT_COUNT--;
        pthread_cond_signal(&UI_EVENT_SPACE);
}

// @@ Reset only during startup, or after joining all producers and consumers.
void ui_events_reset(void) {
        pthread_mutex_lock(&UI_EVENT_LOCK);
        UI_EVENT_HEAD = 0;
        UI_EVENT_COUNT = 0;
        UI_EVENT_PEAK = 0;
        UI_EVENT_CLOSED = 0;
        pthread_cond_broadcast(&UI_EVENT_AVAILABLE);
        pthread_cond_broadcast(&UI_EVENT_SPACE);
        pthread_mutex_unlock(&UI_EVENT_LOCK);
}

int ui_events_post(const ui_event_t *event) {
        int result = -1;
        if (!event) return -1;
        pthread_mutex_lock(&UI_EVENT_LOCK);
        if (!UI_EVENT_CLOSED && ui_events_merge_scroll_locked(event)) {
                result = 0;
        } else if (!UI_EVENT_CLOSED && UI_EVENT_COUNT < UI_EVENT_CAPACITY) {
                ui_events_push_locked(event);
                result = 0;
        }
        pthread_mutex_unlock(&UI_EVENT_LOCK);
        return result;
}

// @@ A worker may wait for space. A closed queue rejects further production.
int ui_events_post_wait(const ui_event_t *event) {
        if (!event) return -1;
        pthread_mutex_lock(&UI_EVENT_LOCK);
        while (!UI_EVENT_CLOSED && UI_EVENT_COUNT == UI_EVENT_CAPACITY) {
                if (ui_events_merge_scroll_locked(event)) {
                        pthread_mutex_unlock(&UI_EVENT_LOCK);
                        return 0;
                }
                pthread_cond_wait(&UI_EVENT_SPACE, &UI_EVENT_LOCK);
        }
        if (UI_EVENT_CLOSED) {
                pthread_mutex_unlock(&UI_EVENT_LOCK);
                return -1;
        }
        if (!ui_events_merge_scroll_locked(event))
                ui_events_push_locked(event);
        pthread_mutex_unlock(&UI_EVENT_LOCK);
        return 0;
}

// @@ The final barrier and close are one mutex-protected transition.
// Producers that race with shutdown either precede this barrier or fail.
int ui_events_post_and_close(const ui_event_t *event) {
        if (!event) return -1;
        pthread_mutex_lock(&UI_EVENT_LOCK);
        while (!UI_EVENT_CLOSED && UI_EVENT_COUNT == UI_EVENT_CAPACITY)
                pthread_cond_wait(&UI_EVENT_SPACE, &UI_EVENT_LOCK);
        if (UI_EVENT_CLOSED) {
                pthread_mutex_unlock(&UI_EVENT_LOCK);
                return -1;
        }
        ui_events_push_locked(event);
        UI_EVENT_CLOSED = 1;
        pthread_cond_broadcast(&UI_EVENT_AVAILABLE);
        pthread_cond_broadcast(&UI_EVENT_SPACE);
        pthread_mutex_unlock(&UI_EVENT_LOCK);
        return 0;
}

int ui_events_next(ui_event_t *event) {
        int result = 0;
        if (!event) return -1;
        pthread_mutex_lock(&UI_EVENT_LOCK);
        if (UI_EVENT_COUNT > 0) {
                ui_events_pop_locked(event);
                result = 1;
        }
        pthread_mutex_unlock(&UI_EVENT_LOCK);
        return result;
}

// @@ Consumer drains accepted events even after shutdown has been requested.
int ui_events_wait_next(ui_event_t *event) {
        if (!event) return -1;
        pthread_mutex_lock(&UI_EVENT_LOCK);
        while (!UI_EVENT_CLOSED && UI_EVENT_COUNT == 0)
                pthread_cond_wait(&UI_EVENT_AVAILABLE, &UI_EVENT_LOCK);
        if (UI_EVENT_COUNT == 0) {
                pthread_mutex_unlock(&UI_EVENT_LOCK);
                return 0;
        }
        ui_events_pop_locked(event);
        pthread_mutex_unlock(&UI_EVENT_LOCK);
        return 1;
}

void ui_events_close(void) {
        pthread_mutex_lock(&UI_EVENT_LOCK);
        UI_EVENT_CLOSED = 1;
        pthread_cond_broadcast(&UI_EVENT_AVAILABLE);
        pthread_cond_broadcast(&UI_EVENT_SPACE);
        pthread_mutex_unlock(&UI_EVENT_LOCK);
}

unsigned int ui_events_pending(void) {
        unsigned int pending;
        pthread_mutex_lock(&UI_EVENT_LOCK);
        pending = UI_EVENT_COUNT;
        pthread_mutex_unlock(&UI_EVENT_LOCK);
        return pending;
}

// @@ Diagnostic: maximum pending events since the last UI startup/reset.
unsigned int ui_events_high_watermark(void) {
        unsigned int peak;
        pthread_mutex_lock(&UI_EVENT_LOCK);
        peak = UI_EVENT_PEAK;
        pthread_mutex_unlock(&UI_EVENT_LOCK);
        return peak;
}
