// Copyright 2026 Jamison A. Drapeau
// Monotonic receive deadline: a busy capture interface cannot extend a scan.
#ifndef KWIRE_DEADLINE_H
#define KWIRE_DEADLINE_H
#include <stdint.h>
#include <time.h>

#define KWIRE_RX_HARD_TIMEOUT_MS 30000
#define KWIRE_RX_DEFAULT_TIMEOUT_MS 1000

typedef struct KWIRE_DEADLINE {
        uint64_t EXPIRES_NS;
} KWIRE_DEADLINE;

static inline int kwire_deadline_start(
        KWIRE_DEADLINE *DEADLINE, int32_t TIMEOUT_MS
) {
        struct timespec NOW;
        if (!DEADLINE || clock_gettime(CLOCK_MONOTONIC, &NOW) != 0) return -1;
        if (TIMEOUT_MS <= 0) TIMEOUT_MS = KWIRE_RX_DEFAULT_TIMEOUT_MS;
        if (TIMEOUT_MS > KWIRE_RX_HARD_TIMEOUT_MS) {
                TIMEOUT_MS = KWIRE_RX_HARD_TIMEOUT_MS;
        }
        DEADLINE->EXPIRES_NS = (uint64_t)NOW.tv_sec * 1000000000ULL
                + (uint64_t)NOW.tv_nsec
                + (uint64_t)TIMEOUT_MS * 1000000ULL;
        return 0;
}

// >0: remaining milliseconds, 0: expired, -1: clock failure.
// Round UP to avoid spinning on sub-millisecond remaining intervals.
static inline int32_t kwire_deadline_remaining_ms(
        const KWIRE_DEADLINE *DEADLINE
) {
        struct timespec NOW;
        uint64_t NOW_NS;
        uint64_t REMAINING_NS;
        if (!DEADLINE || clock_gettime(CLOCK_MONOTONIC, &NOW) != 0) return -1;
        NOW_NS = (uint64_t)NOW.tv_sec * 1000000000ULL + (uint64_t)NOW.tv_nsec;
        if (NOW_NS >= DEADLINE->EXPIRES_NS) return 0;
        REMAINING_NS = DEADLINE->EXPIRES_NS - NOW_NS;
        return (int32_t)((REMAINING_NS + 999999ULL) / 1000000ULL);
}

#endif
