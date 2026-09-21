// Copyright 2026 Jamison A. Drapeau
// Internal bounded state for authorized project scan execution.
#ifndef __KSCAN_INTERNAL__H
#define __KSCAN_INTERNAL__H

#include "data.h"
#include "kworker.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/socket.h>

#define KSCAN_HARD_PENDING        65535U
#define KSCAN_HARD_BATCH          65535U
#define KSCAN_HARD_WORKERS          256U
#define KSCAN_ICMPV4_ECHO_ID     0x6869U
#define KSCAN_IDLE_SLEEP_NS      1000000L
#define KSCAN_IPV4_HEADER_MAX          20U
#define KSCAN_ETHERNET_HEADER_MAX      14U

typedef enum KSCAN_EVENT_TYPE {
        KSCAN_EVENT_RESULT = 0,
        KSCAN_EVENT_BUILD_ERROR,
        KSCAN_EVENT_NO_IPV4,
        KSCAN_EVENT_TX_ERROR,
        KSCAN_EVENT_WARNING,
        KSCAN_EVENT_ERROR,
        KSCAN_EVENT_CANCELLED,
        KSCAN_EVENT_COMPLETE
} KSCAN_EVENT_TYPE;

typedef enum KSCAN_TRANSACTION_STATE {
        KSCAN_TRANSACTION_NEW = 0,
        KSCAN_TRANSACTION_READY,
        KSCAN_TRANSACTION_PENDING,
        KSCAN_TRANSACTION_RETIRED
} KSCAN_TRANSACTION_STATE;

typedef struct KSCAN_PTR_QUEUE {
        void ** ITEMS;
        size_t CAPACITY;
        size_t HEAD;
        size_t TAIL;
        size_t COUNT;
        pthread_mutex_t MUTEX;
} KSCAN_PTR_QUEUE;

typedef struct KSCAN_TRANSACTION {
        struct KSCAN_SESSION * SESSION;
        unsigned char TID[TID_BLOCK];
        nosix_tx_packet_t PACKET;
        uint8_t UPPER[SUPSUP_BLOCK];
        size_t UPPER_LENGTH;
        uint16_t SEQUENCE;
        KSCAN_TRANSACTION_STATE STATE;
        uint64_t CREATED_NS;
        uint64_t TX_NS;
        uint64_t DEADLINE_NS;
        uint64_t COMPLETED_NS;
        size_t WIRE_LENGTH;
        uint8_t RESULT;
        nosix_status_t NETWORK_STATUS;
        uint8_t * TX_FRAME;
        size_t TX_FRAME_CAPACITY;
        size_t TX_FRAME_LENGTH;
        size_t CAPTURE_RESERVED;
        struct KSCAN_TRANSACTION * PENDING_PREV;
        struct KSCAN_TRANSACTION * PENDING_NEXT;
} KSCAN_TRANSACTION;

typedef struct KSCAN_EVENT {
        KSCAN_EVENT_TYPE TYPE;
        unsigned char TID[TID_BLOCK];
        uint8_t RESULT;
        nosix_status_t NETWORK_STATUS;
        uint64_t TIMESTAMP_NS;
} KSCAN_EVENT;

typedef struct KSCAN_BUILD_JOB {
        struct KSCAN_SESSION * SESSION;
        unsigned char TID[TID_BLOCK];
} KSCAN_BUILD_JOB;

typedef struct KSCAN_SESSION {
        _carry_forward * PROG_DATA;
        KWORKER_POOL * WORKERS;
        KSCAN_PTR_QUEUE READY_QUEUE;
        KSCAN_PTR_QUEUE EVENT_QUEUE;
        FLOWER * NEXT_TARGET;
        KSCAN_TRANSACTION * PENDING_HEAD;
        KSCAN_TRANSACTION * PENDING_TAIL;
        unsigned int WORKER_COUNT;
        unsigned int PENDING_LIMIT;
        unsigned int BATCH_LIMIT;
        unsigned int PENDING_COUNT;
        unsigned int STAGED_TARGETS;
        unsigned int CAPTURE_LIMIT_PER_TX;
        uint64_t CAPTURE_LIMIT_TOTAL;
        uint64_t CAPTURE_BYTES_ACTIVE;
        int8_t CAPTURE_BLOCKED;
        int8_t TX_INTERFACE_AUTO;
        int8_t TX_AUTO_LOCKED;
        uint64_t TARGET_TOTAL;
        uint64_t TARGET_DISPATCHED;
        uint64_t TARGET_SENT;
        uint64_t TARGET_RETIRED;
        uint64_t TARGET_ERROR_COUNT;
        uint64_t TARGET_SKIP_COUNT;
        uint64_t NEXT_TX_NS;
        uint64_t TX_INTERVAL_NS;
        atomic_uint BUILD_OUTSTANDING;
        atomic_uint NEXT_SEQUENCE;
        atomic_int CANCELLING;
        atomic_int FATAL;
} KSCAN_SESSION;

void kscan_worker_build(void * PAYLOAD);

#endif
