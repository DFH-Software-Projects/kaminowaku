// Copyright 2026 Jamison A. Drapeau
// Internal bounded dispatcher queues for authorized project scan work.
#include "kscan_dispatch.h"
#include "kscan_internal.h"

#include <stdlib.h>
#include <string.h>

static int kscan_queue_open(
        KSCAN_PTR_QUEUE * QUEUE,
        size_t CAPACITY
) {
        if (!QUEUE || CAPACITY == 0) {
                return ABNORMAL;
        }

        memset(QUEUE, 0x00, sizeof(*QUEUE));
        QUEUE->ITEMS = calloc(CAPACITY, sizeof(*QUEUE->ITEMS));
        if (!QUEUE->ITEMS) {
                return ABNORMAL;
        }

        QUEUE->CAPACITY = CAPACITY;

        if (pthread_mutex_init(&QUEUE->MUTEX, NULL) != 0) {
                free(QUEUE->ITEMS);
                QUEUE->ITEMS = NULL;
                QUEUE->CAPACITY = 0;
                return ABNORMAL;
        }

        return NORMAL;
}

static void kscan_queue_close(
        KSCAN_PTR_QUEUE * QUEUE
) {
        if (!QUEUE || !QUEUE->ITEMS) {
                return;
        }

        pthread_mutex_destroy(&QUEUE->MUTEX);
        free(QUEUE->ITEMS);
        memset(QUEUE, 0x00, sizeof(*QUEUE));
}

static int kscan_queue_push(
        KSCAN_PTR_QUEUE * QUEUE,
        void * ITEM
) {
        if (!QUEUE || !QUEUE->ITEMS || !ITEM) {
                return ABNORMAL;
        }

        pthread_mutex_lock(&QUEUE->MUTEX);

        if (QUEUE->COUNT >= QUEUE->CAPACITY) {
                pthread_mutex_unlock(&QUEUE->MUTEX);
                return ABNORMAL;
        }

        QUEUE->ITEMS[QUEUE->TAIL] = ITEM;
        QUEUE->TAIL = (
                QUEUE->TAIL + 1
        ) % QUEUE->CAPACITY;
        QUEUE->COUNT++;

        pthread_mutex_unlock(&QUEUE->MUTEX);
        return NORMAL;
}

static void * kscan_queue_pop(
        KSCAN_PTR_QUEUE * QUEUE
) {
        void * ITEM;

        if (!QUEUE || !QUEUE->ITEMS) {
                return NULL;
        }

        pthread_mutex_lock(&QUEUE->MUTEX);

        if (QUEUE->COUNT == 0) {
                pthread_mutex_unlock(&QUEUE->MUTEX);
                return NULL;
        }

        ITEM = QUEUE->ITEMS[QUEUE->HEAD];
        QUEUE->ITEMS[QUEUE->HEAD] = NULL;
        QUEUE->HEAD = (
                QUEUE->HEAD + 1
        ) % QUEUE->CAPACITY;
        QUEUE->COUNT--;

        pthread_mutex_unlock(&QUEUE->MUTEX);
        return ITEM;
}

static size_t kscan_queue_count(
        KSCAN_PTR_QUEUE * QUEUE
) {
        size_t COUNT;

        if (!QUEUE || !QUEUE->ITEMS) {
                return 0;
        }

        pthread_mutex_lock(&QUEUE->MUTEX);
        COUNT = QUEUE->COUNT;
        pthread_mutex_unlock(&QUEUE->MUTEX);
        return COUNT;
}

static void kscan_dispatch_build_cancel(
        void * PAYLOAD
) {
        KSCAN_BUILD_JOB * BUILD_JOB = (KSCAN_BUILD_JOB *)PAYLOAD;

        if (!BUILD_JOB) {
                return;
        }

        if (BUILD_JOB->SESSION) {
                atomic_fetch_sub(
                        &BUILD_JOB->SESSION->BUILD_OUTSTANDING,
                        1U
                );
        }

        free(BUILD_JOB);
}

int kscan_dispatcher_open(
        KSCAN_SESSION * SESSION
) {
        size_t READY_CAPACITY;
        size_t EVENT_CAPACITY;

        if (!SESSION) {
                return ABNORMAL;
        }

        READY_CAPACITY = SESSION->BATCH_LIMIT;
        EVENT_CAPACITY = SESSION->BATCH_LIMIT;

        if (READY_CAPACITY == 0 || EVENT_CAPACITY == 0) {
                return ABNORMAL;
        }

        if (
                kscan_queue_open(
                        &SESSION->READY_QUEUE,
                        READY_CAPACITY
                ) != NORMAL
        ) {
                return ABNORMAL;
        }

        if (
                kscan_queue_open(
                        &SESSION->EVENT_QUEUE,
                        EVENT_CAPACITY
                ) != NORMAL
        ) {
                kscan_queue_close(&SESSION->READY_QUEUE);
                return ABNORMAL;
        }

        return NORMAL;
}

void kscan_dispatcher_close(
        KSCAN_SESSION * SESSION
) {
        if (!SESSION) {
                return;
        }

        kscan_queue_close(&SESSION->READY_QUEUE);
        kscan_queue_close(&SESSION->EVENT_QUEUE);
}

int kscan_dispatch_build(
        KSCAN_SESSION * SESSION,
        const unsigned char * TID
) {
        KSCAN_BUILD_JOB * BUILD_JOB;
        KWORKER_JOB WORKER_JOB;
        int STATUS;

        if (
                !SESSION
                || !SESSION->WORKERS
                || !TID
                || TID[0] == 0x00
        ) {
                return KSCAN_DISPATCH_ERROR;
        }

        BUILD_JOB = calloc(1, sizeof(*BUILD_JOB));
        if (!BUILD_JOB) {
                return KSCAN_DISPATCH_ERROR;
        }

        BUILD_JOB->SESSION = SESSION;
        memcpy(BUILD_JOB->TID, TID, TID_BLOCK);

        memset(&WORKER_JOB, 0x00, sizeof(WORKER_JOB));
        WORKER_JOB.RUN = kscan_worker_build;
        WORKER_JOB.PAYLOAD = BUILD_JOB;
        WORKER_JOB.CANCEL = kscan_dispatch_build_cancel;

        atomic_fetch_add(&SESSION->BUILD_OUTSTANDING, 1U);

        STATUS = kworker_dispatch(
                SESSION->WORKERS,
                &WORKER_JOB
        );

        if (STATUS == KWORKER_DEFER) {
                atomic_fetch_sub(&SESSION->BUILD_OUTSTANDING, 1U);
                free(BUILD_JOB);
                return KSCAN_DISPATCH_DEFER;
        }

        if (STATUS != KWORKER_OK) {
                atomic_fetch_sub(&SESSION->BUILD_OUTSTANDING, 1U);
                free(BUILD_JOB);
                return KSCAN_DISPATCH_ERROR;
        }

        return KSCAN_DISPATCH_OK;
}

int kscan_dispatch_ready(
        KSCAN_SESSION * SESSION,
        KSCAN_TRANSACTION * TRANSACTION
) {
        if (!SESSION || !TRANSACTION) {
                return KSCAN_DISPATCH_ERROR;
        }

        return kscan_queue_push(
                &SESSION->READY_QUEUE,
                TRANSACTION
        ) == NORMAL
                ? KSCAN_DISPATCH_OK
                : KSCAN_DISPATCH_ERROR;
}

KSCAN_TRANSACTION * kscan_dispatch_ready_pop(
        KSCAN_SESSION * SESSION
) {
        if (!SESSION) {
                return NULL;
        }

        return (KSCAN_TRANSACTION *)kscan_queue_pop(
                &SESSION->READY_QUEUE
        );
}

size_t kscan_dispatch_ready_count(
        KSCAN_SESSION * SESSION
) {
        if (!SESSION) {
                return 0;
        }

        return kscan_queue_count(
                &SESSION->READY_QUEUE
        );
}

int kscan_dispatch_event(
        KSCAN_SESSION * SESSION,
        KSCAN_EVENT * EVENT
) {
        if (!SESSION || !EVENT) {
                return KSCAN_DISPATCH_ERROR;
        }

        return kscan_queue_push(
                &SESSION->EVENT_QUEUE,
                EVENT
        ) == NORMAL
                ? KSCAN_DISPATCH_OK
                : KSCAN_DISPATCH_ERROR;
}

KSCAN_EVENT * kscan_dispatch_event_pop(
        KSCAN_SESSION * SESSION
) {
        if (!SESSION) {
                return NULL;
        }

        return (KSCAN_EVENT *)kscan_queue_pop(
                &SESSION->EVENT_QUEUE
        );
}

size_t kscan_dispatch_event_count(
        KSCAN_SESSION * SESSION
) {
        if (!SESSION) {
                return 0;
        }

        return kscan_queue_count(
                &SESSION->EVENT_QUEUE
        );
}
