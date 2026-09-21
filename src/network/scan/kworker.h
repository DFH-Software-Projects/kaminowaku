// Copyright 2026 Jamison A. Drapeau
#ifndef __KWORKER__H
#define __KWORKER__H
#include <stddef.h>
typedef void (*kworker_job_fn)(void *);
typedef void (*kworker_cancel_fn)(void *);
typedef struct KWORKER_JOB {
        kworker_job_fn RUN;
        void * PAYLOAD;
        kworker_cancel_fn CANCEL;
} KWORKER_JOB;
typedef struct KWORKER_POOL KWORKER_POOL;
typedef enum { KWORKER_OK = 0, KWORKER_DEFER = 1, KWORKER_ERROR = -1 } kworker_status_t;
int kworker_pool_open(KWORKER_POOL ** POOL, unsigned int THREAD_COUNT, size_t QUEUE_CAPACITY);
int kworker_dispatch(KWORKER_POOL * POOL, const KWORKER_JOB * JOB);
void kworker_pool_cancel(KWORKER_POOL ** POOL);
void kworker_pool_close(KWORKER_POOL ** POOL);
#endif
