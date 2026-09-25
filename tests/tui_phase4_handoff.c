// Copyright 2026 Jamison A. Drapeau
// @@ Stop/drain/join before fork, then restart the same event transport.
#include "ui_events.h"

#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct {
        pthread_mutex_t lock;
        pthread_cond_t ready;
        int done;
} TEST_ACK;

static unsigned int RECEIVED = 0;

static void *render_worker(void *unused) {
        ui_event_t event;
        (void)unused;
        while (ui_events_wait_next(&event) == 1) {
                if (event.type == UI_EVENT_OUTPUT) RECEIVED++;
                if (event.type == UI_EVENT_STOP) {
                        TEST_ACK *ack = (TEST_ACK *)event.completion;
                        assert(ack != NULL);
                        pthread_mutex_lock(&ack->lock);
                        ack->done = 1;
                        pthread_cond_signal(&ack->ready);
                        pthread_mutex_unlock(&ack->lock);
                        return NULL;
                }
        }
        return NULL;
}

static void stop_worker(pthread_t thread) {
        TEST_ACK ack = {0};
        ui_event_t event = {0};
        assert(pthread_mutex_init(&ack.lock, NULL) == 0);
        assert(pthread_cond_init(&ack.ready, NULL) == 0);
        event.type = UI_EVENT_STOP;
        event.completion = &ack;
        assert(ui_events_post_and_close(&event) == 0);
        // @@ No new work may appear behind the terminal owner's stop event.
        assert(ui_events_post_wait(&event) == -1);
        pthread_mutex_lock(&ack.lock);
        while (!ack.done)
                pthread_cond_wait(&ack.ready, &ack.lock);
        pthread_mutex_unlock(&ack.lock);
        assert(pthread_join(thread, NULL) == 0);
        assert(ui_events_pending() == 0);
        pthread_cond_destroy(&ack.ready);
        pthread_mutex_destroy(&ack.lock);
}

int main(void) {
        pthread_t thread;
        ui_event_t event = {0};
        int status;

        ui_events_reset();
        assert(pthread_create(&thread, NULL, render_worker, NULL) == 0);
        event.type = UI_EVENT_OUTPUT;
        for (unsigned int i = 0; i < 1000; i++)
                assert(ui_events_post_wait(&event) == 0);
        stop_worker(thread);
        assert(RECEIVED == 1000);

        // @@ No other threads exist when the interactive tool child forks.
        pid_t child = fork();
        assert(child >= 0);
        if (child == 0) _exit(0);
        assert(waitpid(child, &status, 0) == child);
        assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);

        ui_events_reset();
        assert(pthread_create(&thread, NULL, render_worker, NULL) == 0);
        for (unsigned int i = 0; i < 1000; i++)
                assert(ui_events_post_wait(&event) == 0);
        stop_worker(thread);
        assert(RECEIVED == 2000);
        ui_events_close();
        puts("PASS: event drain, join, fork and renderer restart");
        return 0;
}
