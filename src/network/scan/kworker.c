// Copyright 2026 Jamison A. Drapeau
// Bounded local worker pool for authorized project scan jobs.
#include "kworker.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

struct KWORKER_POOL {
        pthread_t * THREADS;
        unsigned int THREAD_COUNT;
        KWORKER_JOB * QUEUE;
        size_t QUEUE_CAPACITY;
        size_t QUEUE_HEAD;
        size_t QUEUE_TAIL;
        size_t QUEUE_COUNT;
        int STOPPING;
        pthread_mutex_t MUTEX;
        pthread_cond_t HAS_WORK;
};

static void * kworker_thread(void * ARGUMENT) {
        KWORKER_POOL * POOL = (KWORKER_POOL *)ARGUMENT;

        if (!POOL) {
                return NULL;
        }

        for (;;) {
                KWORKER_JOB JOB;
                memset(&JOB, 0x00, sizeof(JOB));

                pthread_mutex_lock(&POOL->MUTEX);

                while (
                        POOL->QUEUE_COUNT == 0
                        && !POOL->STOPPING
                ) {
                        pthread_cond_wait(
                                &POOL->HAS_WORK,
                                &POOL->MUTEX
                        );
                }

                if (
                        POOL->QUEUE_COUNT == 0
                        && POOL->STOPPING
                ) {
                        pthread_mutex_unlock(&POOL->MUTEX);
                        break;
                }

                JOB = POOL->QUEUE[POOL->QUEUE_HEAD];
                memset(
                        &POOL->QUEUE[POOL->QUEUE_HEAD],
                        0x00,
                        sizeof(KWORKER_JOB)
                );

                POOL->QUEUE_HEAD = (
                        POOL->QUEUE_HEAD + 1
                ) % POOL->QUEUE_CAPACITY;
                POOL->QUEUE_COUNT--;

                pthread_mutex_unlock(&POOL->MUTEX);

                if (JOB.RUN) {
                        JOB.RUN(JOB.PAYLOAD);
                }
        }

        return NULL;
}

static void kworker_pool_destroy(
        KWORKER_POOL ** POOL
) {
        unsigned int INDEX;
        KWORKER_POOL * ACTIVE_POOL;

        if (!POOL || !*POOL) {
                return;
        }

        ACTIVE_POOL = *POOL;

        for (
                INDEX = 0;
                INDEX < ACTIVE_POOL->THREAD_COUNT;
                INDEX++
        ) {
                pthread_join(
                        ACTIVE_POOL->THREADS[INDEX],
                        NULL
                );
        }

        pthread_cond_destroy(&ACTIVE_POOL->HAS_WORK);
        pthread_mutex_destroy(&ACTIVE_POOL->MUTEX);
        free(ACTIVE_POOL->THREADS);
        free(ACTIVE_POOL->QUEUE);
        free(ACTIVE_POOL);
        *POOL = NULL;
}

int kworker_pool_open(
        KWORKER_POOL ** POOL,
        unsigned int THREAD_COUNT,
        size_t QUEUE_CAPACITY
) {
        KWORKER_POOL * NEW_POOL;
        unsigned int INDEX;

        if (
                !POOL
                || THREAD_COUNT == 0
                || QUEUE_CAPACITY == 0
        ) {
                return KWORKER_ERROR;
        }

        *POOL = NULL;
        NEW_POOL = calloc(1, sizeof(*NEW_POOL));
        if (!NEW_POOL) {
                return KWORKER_ERROR;
        }

        NEW_POOL->THREADS = calloc(
                THREAD_COUNT,
                sizeof(*NEW_POOL->THREADS)
        );
        NEW_POOL->QUEUE = calloc(
                QUEUE_CAPACITY,
                sizeof(*NEW_POOL->QUEUE)
        );

        if (
                !NEW_POOL->THREADS
                || !NEW_POOL->QUEUE
        ) {
                free(NEW_POOL->THREADS);
                free(NEW_POOL->QUEUE);
                free(NEW_POOL);
                return KWORKER_ERROR;
        }

        NEW_POOL->THREAD_COUNT = THREAD_COUNT;
        NEW_POOL->QUEUE_CAPACITY = QUEUE_CAPACITY;

        if (pthread_mutex_init(&NEW_POOL->MUTEX, NULL) != 0) {
                free(NEW_POOL->THREADS);
                free(NEW_POOL->QUEUE);
                free(NEW_POOL);
                return KWORKER_ERROR;
        }

        if (pthread_cond_init(&NEW_POOL->HAS_WORK, NULL) != 0) {
                pthread_mutex_destroy(&NEW_POOL->MUTEX);
                free(NEW_POOL->THREADS);
                free(NEW_POOL->QUEUE);
                free(NEW_POOL);
                return KWORKER_ERROR;
        }

        for (INDEX = 0; INDEX < THREAD_COUNT; INDEX++) {
                if (
                        pthread_create(
                                &NEW_POOL->THREADS[INDEX],
                                NULL,
                                kworker_thread,
                                NEW_POOL
                        ) != 0
                ) {
                        pthread_mutex_lock(&NEW_POOL->MUTEX);
                        NEW_POOL->STOPPING = 1;
                        pthread_cond_broadcast(&NEW_POOL->HAS_WORK);
                        pthread_mutex_unlock(&NEW_POOL->MUTEX);

                        while (INDEX > 0) {
                                INDEX--;
                                pthread_join(
                                        NEW_POOL->THREADS[INDEX],
                                        NULL
                                );
                        }

                        pthread_cond_destroy(&NEW_POOL->HAS_WORK);
                        pthread_mutex_destroy(&NEW_POOL->MUTEX);
                        free(NEW_POOL->THREADS);
                        free(NEW_POOL->QUEUE);
                        free(NEW_POOL);
                        return KWORKER_ERROR;
                }
        }

        *POOL = NEW_POOL;
        return KWORKER_OK;
}

int kworker_dispatch(
        KWORKER_POOL * POOL,
        const KWORKER_JOB * JOB
) {
        if (
                !POOL
                || !JOB
                || !JOB->RUN
        ) {
                return KWORKER_ERROR;
        }

        pthread_mutex_lock(&POOL->MUTEX);

        if (POOL->STOPPING) {
                pthread_mutex_unlock(&POOL->MUTEX);
                return KWORKER_ERROR;
        }

        if (POOL->QUEUE_COUNT >= POOL->QUEUE_CAPACITY) {
                pthread_mutex_unlock(&POOL->MUTEX);
                return KWORKER_DEFER;
        }

        POOL->QUEUE[POOL->QUEUE_TAIL] = *JOB;
        POOL->QUEUE_TAIL = (
                POOL->QUEUE_TAIL + 1
        ) % POOL->QUEUE_CAPACITY;
        POOL->QUEUE_COUNT++;

        pthread_cond_signal(&POOL->HAS_WORK);
        pthread_mutex_unlock(&POOL->MUTEX);
        return KWORKER_OK;
}

void kworker_pool_cancel(
        KWORKER_POOL ** POOL
) {
        KWORKER_POOL * ACTIVE_POOL;

        if (!POOL || !*POOL) {
                return;
        }

        ACTIVE_POOL = *POOL;

        pthread_mutex_lock(&ACTIVE_POOL->MUTEX);
        ACTIVE_POOL->STOPPING = 1;

        while (ACTIVE_POOL->QUEUE_COUNT > 0) {
                KWORKER_JOB JOB = ACTIVE_POOL->QUEUE[ACTIVE_POOL->QUEUE_HEAD];

                memset(
                        &ACTIVE_POOL->QUEUE[ACTIVE_POOL->QUEUE_HEAD],
                        0x00,
                        sizeof(KWORKER_JOB)
                );

                ACTIVE_POOL->QUEUE_HEAD = (
                        ACTIVE_POOL->QUEUE_HEAD + 1
                ) % ACTIVE_POOL->QUEUE_CAPACITY;
                ACTIVE_POOL->QUEUE_COUNT--;

                if (JOB.CANCEL) {
                        JOB.CANCEL(JOB.PAYLOAD);
                }
        }

        ACTIVE_POOL->QUEUE_TAIL = ACTIVE_POOL->QUEUE_HEAD;
        pthread_cond_broadcast(&ACTIVE_POOL->HAS_WORK);
        pthread_mutex_unlock(&ACTIVE_POOL->MUTEX);

        kworker_pool_destroy(POOL);
}

void kworker_pool_close(
        KWORKER_POOL ** POOL
) {
        KWORKER_POOL * ACTIVE_POOL;

        if (!POOL || !*POOL) {
                return;
        }

        ACTIVE_POOL = *POOL;

        pthread_mutex_lock(&ACTIVE_POOL->MUTEX);
        ACTIVE_POOL->STOPPING = 1;
        pthread_cond_broadcast(&ACTIVE_POOL->HAS_WORK);
        pthread_mutex_unlock(&ACTIVE_POOL->MUTEX);

        kworker_pool_destroy(POOL);
}
