// Copyright 2026 Jamison A. Drapeau
// Regression: unrelated capture traffic must not extend an RX deadline.
#define _POSIX_C_SOURCE 200809L
#include "kwire_deadline.h"

#include <assert.h>
#include <stdio.h>
#include <time.h>

static void sleep_ms(long milliseconds) {
        struct timespec duration;
        duration.tv_sec = milliseconds / 1000;
        duration.tv_nsec = (milliseconds % 1000) * 1000000L;
        while (nanosleep(&duration, &duration) < 0) {
                ;
        }
}

int main(void) {
        KWIRE_DEADLINE deadline;
        int32_t remaining;

        assert(kwire_deadline_start(&deadline, 1) == 0);
        sleep_ms(8);
        assert(kwire_deadline_remaining_ms(&deadline) == 0);

        assert(kwire_deadline_start(&deadline, INT32_MAX) == 0);
        remaining = kwire_deadline_remaining_ms(&deadline);
        assert(remaining > 0 && remaining <= KWIRE_RX_HARD_TIMEOUT_MS);

        assert(kwire_deadline_start(&deadline, -1) == 0);
        remaining = kwire_deadline_remaining_ms(&deadline);
        assert(remaining > 0 && remaining <= KWIRE_RX_DEFAULT_TIMEOUT_MS);

        // Multiple reads of remaining time must not reset the original clock.
        assert(kwire_deadline_start(&deadline, 40) == 0);
        for (int i = 0; i < 10; i++) {
                remaining = kwire_deadline_remaining_ms(&deadline);
                assert(remaining >= 0 && remaining <= 40);
                sleep_ms(7);
        }
        assert(kwire_deadline_remaining_ms(&deadline) == 0);
        puts("Kaminowaku absolute receive deadline: PASS");
        return 0;
}
