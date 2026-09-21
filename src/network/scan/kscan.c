// Copyright 2026 Jamison A. Drapeau
// Bounded threaded project scan scheduler.
#define _POSIX_C_SOURCE 200809L

#include "kscan.h"
#include "kscan_dispatch.h"
#include "kscan_internal.h"
#include "kwire.h"
#include "tlib.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>

#define KSCAN_ICMPV4_ECHO_REQUEST 8U

// @@ Do not change the on-disk TARGET size when adding scan types
_Static_assert(
        sizeof(TARGET_SCAN_DATA) == LAST_BLOCK,
        "TARGET scan metadata must remain inside the legacy LAST_SCAN_TIME slot"
);

// @@ Nanoseconds since Unix epoch
uint64_t kscan_now_ns(void) {
        struct timespec NOW;
        memset(&NOW, 0x00, sizeof(NOW));

        if (clock_gettime(CLOCK_REALTIME, &NOW) != NORMAL) {
                return 0;
        }

        return (
                ((uint64_t)NOW.tv_sec * 1000000000ULL)
                +
                (uint64_t)NOW.tv_nsec
        );
}

// @@ Human readable nanosecond timestamp
void kscan_format_ns(
        uint64_t TIMESTAMP_NS,
        char * BUFFER,
        size_t BUFFER_SIZE
) {
        time_t SECONDS;
        long NANOSECONDS;
        struct tm TIME_INFO;

        if (!BUFFER || BUFFER_SIZE == 0) {
                return;
        }

        memset(BUFFER, 0x00, BUFFER_SIZE);
        memset(&TIME_INFO, 0x00, sizeof(TIME_INFO));

        if (TIMESTAMP_NS == 0) {
                snprintf(BUFFER, BUFFER_SIZE, "NEVER");
                return;
        }

        SECONDS = (time_t)(TIMESTAMP_NS / 1000000000ULL);
        NANOSECONDS = (long)(TIMESTAMP_NS % 1000000000ULL);

        if (localtime_r(&SECONDS, &TIME_INFO) == NULL) {
                snprintf(BUFFER, BUFFER_SIZE, "INVALID");
                return;
        }

        snprintf(
                BUFFER,
                BUFFER_SIZE,
                "%04d-%02d-%02d %02d:%02d:%02d.%09ld",
                TIME_INFO.tm_year + 1900,
                TIME_INFO.tm_mon + 1,
                TIME_INFO.tm_mday,
                TIME_INFO.tm_hour,
                TIME_INFO.tm_min,
                TIME_INFO.tm_sec,
                NANOSECONDS
        );
}

// @@ Check whether the scan slot contains the new binary scan layout
int8_t kscan_data_valid(const TARGET * PETAL) {
        if (!PETAL) {
                return ISFALSE;
        }

        return (
                PETAL->SCAN.SCAN_VERSION == TARGET_SCAN_DATA_VERSION
        ) ? ISTRUE : ISFALSE;
}

// @@ Human readable ICMPv4 result
const char * kscan_icmpv4_result(uint8_t RESULT) {
        if (!(RESULT & SCAN_RESULT_RAN)) {
                return "NEVER";
        }

        if (RESULT & SCAN_RESULT_REPLY) {
                return "REPLY";
        }

        if (RESULT & SCAN_RESULT_TIMEOUT) {
                return "TIMEOUT";
        }

        if (RESULT & SCAN_RESULT_ROUTE) {
                return "ROUTE";
        }

        if (RESULT & SCAN_RESULT_NEIGHBOR) {
                return "NEIGHBOR";
        }

        if (RESULT & SCAN_RESULT_ERROR) {
                return "ERROR";
        }

        return "UNKNOWN";
}

// @@ Persist the most recent ICMPv4 result into the active target
int kscan_record_icmpv4(
        _carry_forward * _prog_data,
        uint8_t RESULT
) {
        uint64_t SCAN_NS;
        char PETAL_PATH[MAX_PATH];
        TARGET_SCAN_DATA PREVIOUS_SCAN;
        TARGET * PETAL;
        int WRITE_STATUS;

        if (
                !_prog_data
                ||
                !_prog_data->active_project_active_target
                ||
                !_prog_data->active_project_active_target->PETAL
        ) {
                return ABNORMAL;
        }

        PETAL = _prog_data->active_project_active_target->PETAL;
        PREVIOUS_SCAN = PETAL->SCAN;

        SCAN_NS = kscan_now_ns();
        if (SCAN_NS == 0) {
                return ABNORMAL;
        }

        if (
                PETAL->SCAN.SCAN_VERSION
                !=
                TARGET_SCAN_DATA_VERSION
        ) {
                memset(
                        &PETAL->SCAN,
                        0x00,
                        sizeof(PETAL->SCAN)
                );
                PETAL->SCAN.SCAN_VERSION = TARGET_SCAN_DATA_VERSION;
        }

        PETAL->SCAN.ICMPV4 = (
                SCAN_RESULT_RAN
                |
                RESULT
        );

        PETAL->SCAN.ICMPV4_SCAN_NS = SCAN_NS;
        PETAL->SCAN.LAST_SCAN_NS = SCAN_NS;

        memset(PETAL_PATH, 0x00, sizeof(PETAL_PATH));

        if (
                build_petal_path(
                        PETAL_PATH,
                        sizeof(PETAL_PATH),
                        _prog_data->wd,
                        _prog_data->active_project,
                        _prog_data->active_project_active_target->TID
                ) != NORMAL
        ) {
                PETAL->SCAN = PREVIOUS_SCAN;
                return ABNORMAL;
        }

        WRITE_STATUS = targets_write_petal_data(
                PETAL_PATH,
                PETAL
        );

        if (WRITE_STATUS != NORMAL) {
                PETAL->SCAN = PREVIOUS_SCAN;
                return ABNORMAL;
        }

        _prog_data->active_project_has_been_scanned = ISTRUE;
        if (SCAN_NS > _prog_data->active_project_last_scan_ns) {
                _prog_data->active_project_last_scan_ns = SCAN_NS;
        }

        return NORMAL;
}

static uint64_t kscan_monotonic_ns(void) {
        struct timespec NOW;

        memset(&NOW, 0x00, sizeof(NOW));
        if (clock_gettime(CLOCK_MONOTONIC, &NOW) != NORMAL) {
                return 0;
        }

        return (
                (uint64_t)NOW.tv_sec * 1000000000ULL
                + (uint64_t)NOW.tv_nsec
        );
}

static uint16_t kscan_checksum16(
        const uint8_t * BUFFER,
        size_t LENGTH
) {
        uint32_t SUM = 0;
        size_t INDEX = 0;

        while (INDEX + 1 < LENGTH) {
                SUM += (uint16_t)(
                        ((uint16_t)BUFFER[INDEX] << 8)
                        |
                        BUFFER[INDEX + 1]
                );
                INDEX += 2;
        }

        if (INDEX < LENGTH) {
                SUM += (uint16_t)(BUFFER[INDEX] << 8);
        }

        while (SUM >> 16) {
                SUM = (SUM & 0xffffU) + (SUM >> 16);
        }

        return (uint16_t)(~SUM);
}

static uint16_t kscan_next_sequence(
        KSCAN_SESSION * SESSION
) {
        uint16_t SEQUENCE;
        unsigned int VALUE;

        if (!SESSION) {
                return 0;
        }

        do {
                VALUE = atomic_fetch_add(
                        &SESSION->NEXT_SEQUENCE,
                        1U
                ) + 1U;
                SEQUENCE = (uint16_t)(VALUE & 0xffffU);
        } while (SEQUENCE == 0);

        return SEQUENCE;
}

static int kscan_event_submit(
        KSCAN_SESSION * SESSION,
        KSCAN_EVENT_TYPE TYPE,
        const unsigned char * TID,
        nosix_status_t NETWORK_STATUS
) {
        KSCAN_EVENT * EVENT;

        if (!SESSION || !TID) {
                return ABNORMAL;
        }

        EVENT = calloc(1, sizeof(*EVENT));
        if (!EVENT) {
                atomic_store(&SESSION->FATAL, ISTRUE);
                return ABNORMAL;
        }

        EVENT->TYPE = TYPE;
        memcpy(EVENT->TID, TID, TID_BLOCK);
        EVENT->NETWORK_STATUS = NETWORK_STATUS;
        EVENT->TIMESTAMP_NS = kscan_now_ns();

        if (
                kscan_dispatch_event(
                        SESSION,
                        EVENT
                ) != KSCAN_DISPATCH_OK
        ) {
                free(EVENT);
                atomic_store(&SESSION->FATAL, ISTRUE);
                return ABNORMAL;
        }

        return NORMAL;
}

static int kscan_build_icmpv4(
        KSCAN_TRANSACTION * TRANSACTION,
        const unsigned char * IPV4
) {
        static const uint8_t PAYLOAD[] =
                "Sent from Kaminowaku|sig=kaminowaku_icmpv4|";
        const size_t HEADER_LENGTH = 8;
        const size_t PAYLOAD_LENGTH = sizeof(PAYLOAD) - 1;
        const size_t TOTAL_LENGTH = HEADER_LENGTH + PAYLOAD_LENGTH;
        uint16_t CHECKSUM;

        if (
                !TRANSACTION
                || !IPV4
                || IPV4[0] == 0x00
                || TOTAL_LENGTH > sizeof(TRANSACTION->UPPER)
        ) {
                return ABNORMAL;
        }

        memset(
                TRANSACTION->UPPER,
                0x00,
                sizeof(TRANSACTION->UPPER)
        );

        TRANSACTION->UPPER[0] = KSCAN_ICMPV4_ECHO_REQUEST;
        TRANSACTION->UPPER[1] = 0x00;
        TRANSACTION->UPPER[4] = (uint8_t)(KSCAN_ICMPV4_ECHO_ID >> 8);
        TRANSACTION->UPPER[5] = (uint8_t)KSCAN_ICMPV4_ECHO_ID;
        TRANSACTION->UPPER[6] = (uint8_t)(TRANSACTION->SEQUENCE >> 8);
        TRANSACTION->UPPER[7] = (uint8_t)TRANSACTION->SEQUENCE;

        memcpy(
                TRANSACTION->UPPER + HEADER_LENGTH,
                PAYLOAD,
                PAYLOAD_LENGTH
        );

        CHECKSUM = kscan_checksum16(
                TRANSACTION->UPPER,
                TOTAL_LENGTH
        );

        TRANSACTION->UPPER[2] = (uint8_t)(CHECKSUM >> 8);
        TRANSACTION->UPPER[3] = (uint8_t)CHECKSUM;
        TRANSACTION->UPPER_LENGTH = TOTAL_LENGTH;

        memset(
                &TRANSACTION->PACKET,
                0x00,
                sizeof(TRANSACTION->PACKET)
        );

        TRANSACTION->PACKET.destination.family = NOSIX_ADDRESS_IPV4;

        if (
                inet_pton(
                        AF_INET,
                        (const char *)IPV4,
                        TRANSACTION->PACKET.destination.bytes.ipv4
                ) != 1
        ) {
                return ABNORMAL;
        }

        TRANSACTION->PACKET.ip_protocol = NOSIX_IPPROTO_ICMP;
        TRANSACTION->PACKET.upper_layer = TRANSACTION->UPPER;
        TRANSACTION->PACKET.upper_layer_length = TRANSACTION->UPPER_LENGTH;
        TRANSACTION->PACKET.flags = 0;
        return NORMAL;
}

void kscan_worker_build(void * PAYLOAD) {
        KSCAN_BUILD_JOB * BUILD_JOB = (KSCAN_BUILD_JOB *)PAYLOAD;
        KSCAN_SESSION * SESSION;
        KSCAN_TRANSACTION * TRANSACTION = NULL;
        TARGET * TARGET_DATA = NULL;
        char * PETAL_PATH = NULL;

        if (!BUILD_JOB) {
                return;
        }

        SESSION = BUILD_JOB->SESSION;
        if (!SESSION) {
                free(BUILD_JOB);
                return;
        }

        if (atomic_load(&SESSION->CANCELLING) == ISTRUE) {
                goto CLEANUP;
        }

        TARGET_DATA = calloc(1, sizeof(*TARGET_DATA));
        PETAL_PATH = calloc(1, MAX_PATH);

        if (!TARGET_DATA || !PETAL_PATH) {
                kscan_event_submit(
                        SESSION,
                        KSCAN_EVENT_BUILD_ERROR,
                        BUILD_JOB->TID,
                        NOSIX_ERR_MEMORY
                );
                goto CLEANUP;
        }

        if (
                build_petal_path(
                        PETAL_PATH,
                        MAX_PATH,
                        SESSION->PROG_DATA->wd,
                        SESSION->PROG_DATA->active_project,
                        BUILD_JOB->TID
                ) != NORMAL
                ||
                targets_read_petal_data(
                        PETAL_PATH,
                        TARGET_DATA
                ) != NORMAL
        ) {
                kscan_event_submit(
                        SESSION,
                        KSCAN_EVENT_BUILD_ERROR,
                        BUILD_JOB->TID,
                        NOSIX_ERR_STATE
                );
                goto CLEANUP;
        }

        if (atomic_load(&SESSION->CANCELLING) == ISTRUE) {
                goto CLEANUP;
        }

        if (TARGET_DATA->IPV4[0] == 0x00) {
                kscan_event_submit(
                        SESSION,
                        KSCAN_EVENT_NO_IPV4,
                        BUILD_JOB->TID,
                        NOSIX_OK
                );
                goto CLEANUP;
        }

        TRANSACTION = calloc(1, sizeof(*TRANSACTION));
        if (!TRANSACTION) {
                kscan_event_submit(
                        SESSION,
                        KSCAN_EVENT_BUILD_ERROR,
                        BUILD_JOB->TID,
                        NOSIX_ERR_MEMORY
                );
                goto CLEANUP;
        }

        TRANSACTION->SESSION = SESSION;
        memcpy(TRANSACTION->TID, BUILD_JOB->TID, TID_BLOCK);
        TRANSACTION->SEQUENCE = kscan_next_sequence(SESSION);
        TRANSACTION->STATE = KSCAN_TRANSACTION_NEW;
        TRANSACTION->CREATED_NS = kscan_monotonic_ns();

        if (
                TRANSACTION->SEQUENCE == 0
                || TRANSACTION->CREATED_NS == 0
                || kscan_build_icmpv4(
                        TRANSACTION,
                        TARGET_DATA->IPV4
                ) != NORMAL
        ) {
                free(TRANSACTION);
                TRANSACTION = NULL;
                kscan_event_submit(
                        SESSION,
                        KSCAN_EVENT_BUILD_ERROR,
                        BUILD_JOB->TID,
                        NOSIX_ERR_ARGUMENT
                );
                goto CLEANUP;
        }

        if (atomic_load(&SESSION->CANCELLING) == ISTRUE) {
                free(TRANSACTION);
                TRANSACTION = NULL;
                goto CLEANUP;
        }

        TRANSACTION->STATE = KSCAN_TRANSACTION_READY;

        if (
                kscan_dispatch_ready(
                        SESSION,
                        TRANSACTION
                ) != KSCAN_DISPATCH_OK
        ) {
                free(TRANSACTION);
                TRANSACTION = NULL;
                kscan_event_submit(
                        SESSION,
                        KSCAN_EVENT_BUILD_ERROR,
                        BUILD_JOB->TID,
                        NOSIX_ERR_STATE
                );
        }

CLEANUP:
        free(TARGET_DATA);
        free(PETAL_PATH);
        atomic_fetch_sub(&SESSION->BUILD_OUTSTANDING, 1U);
        free(BUILD_JOB);
}

static void kscan_transaction_release(
        KSCAN_SESSION * SESSION,
        KSCAN_TRANSACTION * TRANSACTION
) {
        if (!TRANSACTION) {
                return;
        }

        if (TRANSACTION->TX_FRAME) {
                if (
                        SESSION
                        && SESSION->CAPTURE_BYTES_ACTIVE >= TRANSACTION->CAPTURE_RESERVED
                ) {
                        SESSION->CAPTURE_BYTES_ACTIVE -= TRANSACTION->CAPTURE_RESERVED;
                        SESSION->CAPTURE_BLOCKED = ISFALSE;
                }

                free(TRANSACTION->TX_FRAME);
                TRANSACTION->TX_FRAME = NULL;
        }

        TRANSACTION->TX_FRAME_CAPACITY = 0;
        TRANSACTION->TX_FRAME_LENGTH = 0;
        TRANSACTION->CAPTURE_RESERVED = 0;
        free(TRANSACTION);
}

static int kscan_capture_reserve_tx(
        KSCAN_SESSION * SESSION,
        KSCAN_TRANSACTION * TRANSACTION
) {
        size_t REQUIRED;

        if (!SESSION || !TRANSACTION) {
                return KSCAN_DISPATCH_ERROR;
        }

        REQUIRED = (
                TRANSACTION->UPPER_LENGTH
                + KSCAN_IPV4_HEADER_MAX
                + KSCAN_ETHERNET_HEADER_MAX
        );

        if (
                REQUIRED == 0
                || REQUIRED > SESSION->CAPTURE_LIMIT_PER_TX
        ) {
                SESSION->CAPTURE_BLOCKED = ISFALSE;
                return KSCAN_DISPATCH_ERROR;
        }

        if (
                SESSION->CAPTURE_BYTES_ACTIVE
                > SESSION->CAPTURE_LIMIT_TOTAL - REQUIRED
        ) {
                SESSION->CAPTURE_BLOCKED = ISTRUE;
                return KSCAN_DISPATCH_DEFER;
        }

        TRANSACTION->TX_FRAME = calloc(1, REQUIRED);
        if (!TRANSACTION->TX_FRAME) {
                SESSION->CAPTURE_BLOCKED = ISFALSE;
                return KSCAN_DISPATCH_ERROR;
        }

        TRANSACTION->TX_FRAME_CAPACITY = REQUIRED;
        TRANSACTION->CAPTURE_RESERVED = REQUIRED;
        SESSION->CAPTURE_BYTES_ACTIVE += REQUIRED;
        SESSION->CAPTURE_BLOCKED = ISFALSE;
        return KSCAN_DISPATCH_OK;
}

static void kscan_pending_append(
        KSCAN_SESSION * SESSION,
        KSCAN_TRANSACTION * TRANSACTION
) {
        if (!SESSION || !TRANSACTION) {
                return;
        }

        TRANSACTION->PENDING_PREV = SESSION->PENDING_TAIL;
        TRANSACTION->PENDING_NEXT = NULL;

        if (SESSION->PENDING_TAIL) {
                SESSION->PENDING_TAIL->PENDING_NEXT = TRANSACTION;
        } else {
                SESSION->PENDING_HEAD = TRANSACTION;
        }

        SESSION->PENDING_TAIL = TRANSACTION;
        SESSION->PENDING_COUNT++;
}

static void kscan_pending_remove(
        KSCAN_SESSION * SESSION,
        KSCAN_TRANSACTION * TRANSACTION
) {
        if (!SESSION || !TRANSACTION) {
                return;
        }

        if (TRANSACTION->PENDING_PREV) {
                TRANSACTION->PENDING_PREV->PENDING_NEXT = TRANSACTION->PENDING_NEXT;
        } else {
                SESSION->PENDING_HEAD = TRANSACTION->PENDING_NEXT;
        }

        if (TRANSACTION->PENDING_NEXT) {
                TRANSACTION->PENDING_NEXT->PENDING_PREV = TRANSACTION->PENDING_PREV;
        } else {
                SESSION->PENDING_TAIL = TRANSACTION->PENDING_PREV;
        }

        TRANSACTION->PENDING_PREV = NULL;
        TRANSACTION->PENDING_NEXT = NULL;

        if (SESSION->PENDING_COUNT > 0) {
                SESSION->PENDING_COUNT--;
        }
}

static void kscan_retire_expired(
        KSCAN_SESSION * SESSION,
        uint64_t NOW_NS
) {
        KSCAN_TRANSACTION * TRANSACTION;

        if (!SESSION || NOW_NS == 0) {
                return;
        }

        while (
                SESSION->PENDING_HEAD
                && SESSION->PENDING_HEAD->DEADLINE_NS <= NOW_NS
        ) {
                TRANSACTION = SESSION->PENDING_HEAD;
                kscan_pending_remove(
                        SESSION,
                        TRANSACTION
                );

                TRANSACTION->STATE = KSCAN_TRANSACTION_RETIRED;
                SESSION->TARGET_RETIRED++;
                kscan_transaction_release(
                        SESSION,
                        TRANSACTION
                );
        }
}

static void kscan_drain_events(
        KSCAN_SESSION * SESSION
) {
        KSCAN_EVENT * EVENT;

        if (!SESSION) {
                return;
        }

        while (
                (EVENT = kscan_dispatch_event_pop(SESSION)) != NULL
        ) {
                switch (EVENT->TYPE) {
                        case KSCAN_EVENT_NO_IPV4:
                                SESSION->TARGET_SKIP_COUNT++;
                                SESSION->TARGET_RETIRED++;
                                if (SESSION->STAGED_TARGETS > 0) {
                                        SESSION->STAGED_TARGETS--;
                                }
                                break;
                        case KSCAN_EVENT_BUILD_ERROR:
                                SESSION->TARGET_ERROR_COUNT++;
                                SESSION->TARGET_RETIRED++;
                                if (SESSION->STAGED_TARGETS > 0) {
                                        SESSION->STAGED_TARGETS--;
                                }
                                break;
                        case KSCAN_EVENT_TX_ERROR:
                        case KSCAN_EVENT_ERROR:
                                SESSION->TARGET_ERROR_COUNT++;
                                SESSION->TARGET_RETIRED++;
                                break;
                        default:
                                break;
                }

                free(EVENT);
        }
}

static int kscan_refill_targets(
        KSCAN_SESSION * SESSION
) {
        int STATUS;

        if (!SESSION) {
                return ABNORMAL;
        }

        while (
                SESSION->NEXT_TARGET
                && SESSION->STAGED_TARGETS < SESSION->BATCH_LIMIT
                && atomic_load(&SESSION->CANCELLING) != ISTRUE
                && atomic_load(&SESSION->FATAL) != ISTRUE
        ) {
                STATUS = kscan_dispatch_build(
                        SESSION,
                        SESSION->NEXT_TARGET->TID
                );

                if (STATUS == KSCAN_DISPATCH_DEFER) {
                        return NORMAL;
                }

                if (STATUS != KSCAN_DISPATCH_OK) {
                        atomic_store(&SESSION->FATAL, ISTRUE);
                        return ABNORMAL;
                }

                SESSION->NEXT_TARGET = SESSION->NEXT_TARGET->NEXT;
                SESSION->STAGED_TARGETS++;
                SESSION->TARGET_DISPATCHED++;
        }

        return NORMAL;
}

static int kscan_transmit_ready(
        KSCAN_SESSION * SESSION,
        uint64_t NOW_NS
) {
        KSCAN_TRANSACTION * TRANSACTION;
        nosix_status_t STATUS;
        int CAPTURE_STATUS;

        if (!SESSION || NOW_NS == 0) {
                return ABNORMAL;
        }

        if (SESSION->PENDING_COUNT == 0) {
                SESSION->CAPTURE_BLOCKED = ISFALSE;
        }

        if (
                SESSION->PENDING_COUNT >= SESSION->PENDING_LIMIT
                || NOW_NS < SESSION->NEXT_TX_NS
                || (
                        SESSION->CAPTURE_BLOCKED == ISTRUE
                        && SESSION->PENDING_COUNT > 0
                )
                || (
                        SESSION->TX_INTERFACE_AUTO == ISTRUE
                        && SESSION->TX_AUTO_LOCKED != ISTRUE
                        && SESSION->PENDING_COUNT > 0
                )
        ) {
                return NORMAL;
        }

        TRANSACTION = kscan_dispatch_ready_pop(SESSION);
        if (!TRANSACTION) {
                return NORMAL;
        }

        CAPTURE_STATUS = kscan_capture_reserve_tx(
                SESSION,
                TRANSACTION
        );

        if (CAPTURE_STATUS == KSCAN_DISPATCH_DEFER) {
                if (
                        kscan_dispatch_ready(
                                SESSION,
                                TRANSACTION
                        ) != KSCAN_DISPATCH_OK
                ) {
                        kscan_transaction_release(
                                SESSION,
                                TRANSACTION
                        );
                        atomic_store(&SESSION->FATAL, ISTRUE);
                        return ABNORMAL;
                }

                return NORMAL;
        }

        if (CAPTURE_STATUS != KSCAN_DISPATCH_OK) {
                if (SESSION->STAGED_TARGETS > 0) {
                        SESSION->STAGED_TARGETS--;
                }

                kscan_event_submit(
                        SESSION,
                        KSCAN_EVENT_TX_ERROR,
                        TRANSACTION->TID,
                        NOSIX_ERR_MEMORY
                );
                kscan_transaction_release(
                        SESSION,
                        TRANSACTION
                );
                return NORMAL;
        }

        STATUS = kwire_scan_write(
                SESSION->PROG_DATA,
                &TRANSACTION->PACKET,
                TRANSACTION->TX_FRAME,
                TRANSACTION->TX_FRAME_CAPACITY,
                &TRANSACTION->TX_FRAME_LENGTH
        );

        NOW_NS = kscan_monotonic_ns();
        if (NOW_NS == 0) {
                kscan_transaction_release(
                        SESSION,
                        TRANSACTION
                );
                atomic_store(&SESSION->FATAL, ISTRUE);
                return ABNORMAL;
        }

        SESSION->NEXT_TX_NS = NOW_NS + SESSION->TX_INTERVAL_NS;

        if (SESSION->STAGED_TARGETS > 0) {
                SESSION->STAGED_TARGETS--;
        }

        if (STATUS != NOSIX_OK) {
                TRANSACTION->NETWORK_STATUS = STATUS;
                kscan_event_submit(
                        SESSION,
                        KSCAN_EVENT_TX_ERROR,
                        TRANSACTION->TID,
                        STATUS
                );
                kscan_transaction_release(
                        SESSION,
                        TRANSACTION
                );
                return NORMAL;
        }

        if (
                SESSION->TX_INTERFACE_AUTO == ISTRUE
                && SESSION->TX_AUTO_LOCKED != ISTRUE
                && kwire_scan_last_tx_surface(SESSION->PROG_DATA)
                        == NOSIX_TX_SURFACE_ETHERNET
                && kwire_scan_lock_auto_interface(SESSION->PROG_DATA)
                        == NOSIX_OK
        ) {
                SESSION->TX_AUTO_LOCKED = ISTRUE;
        }

        TRANSACTION->WIRE_LENGTH = TRANSACTION->TX_FRAME_LENGTH;
        TRANSACTION->NETWORK_STATUS = NOSIX_OK;
        TRANSACTION->STATE = KSCAN_TRANSACTION_PENDING;
        TRANSACTION->TX_NS = NOW_NS;
        TRANSACTION->DEADLINE_NS = NOW_NS + (
                (uint64_t)SESSION->PROG_DATA->gprof.rx_timeout_ms
                * 1000000ULL
        );

        kscan_pending_append(
                SESSION,
                TRANSACTION
        );

        SESSION->TARGET_SENT++;
        return NORMAL;
}

int kscan_session_open(
        _carry_forward * _prog_data,
        KSCAN_SESSION ** SESSION
) {
        KSCAN_SESSION * NEW_SESSION;
        unsigned int WORKERS;
        unsigned int PENDING;
        unsigned int BATCH;
        unsigned int TX_RATE;

        if (
                !_prog_data
                || !SESSION
                || !_prog_data->active_project_flower
                || !_prog_data->nosix_net
        ) {
                return ABNORMAL;
        }

        *SESSION = NULL;

        WORKERS = _prog_data->gprof.limit_max_scan_workers;
        PENDING = _prog_data->gprof.limit_max_pending_transactions;
        BATCH = _prog_data->gprof.limit_max_targets_per_batch;
        TX_RATE = _prog_data->gprof.limit_max_tx_out_rate_pps;

        if (
                WORKERS == 0
                || WORKERS > KSCAN_HARD_WORKERS
                || PENDING == 0
                || PENDING > KSCAN_HARD_PENDING
                || BATCH == 0
                || BATCH > KSCAN_HARD_BATCH
                || TX_RATE == 0
                || _prog_data->gprof.rx_timeout_ms <= 0
                || _prog_data->gprof.limit_max_capture_bytes_per_tx == 0
                || _prog_data->gprof.limit_max_capture_bytes_total == 0
                || _prog_data->gprof.limit_max_capture_bytes_per_tx
                        > _prog_data->gprof.limit_max_capture_bytes_total
        ) {
                return ABNORMAL;
        }

        if (WORKERS > BATCH) {
                WORKERS = BATCH;
        }

        NEW_SESSION = calloc(1, sizeof(*NEW_SESSION));
        if (!NEW_SESSION) {
                return ABNORMAL;
        }

        NEW_SESSION->PROG_DATA = _prog_data;
        NEW_SESSION->WORKER_COUNT = WORKERS;
        NEW_SESSION->PENDING_LIMIT = PENDING;
        NEW_SESSION->BATCH_LIMIT = BATCH;
        NEW_SESSION->CAPTURE_LIMIT_PER_TX = _prog_data->gprof.limit_max_capture_bytes_per_tx;
        NEW_SESSION->CAPTURE_LIMIT_TOTAL = _prog_data->gprof.limit_max_capture_bytes_total;
        NEW_SESSION->TX_INTERFACE_AUTO = (
                strcmp(
                        (const char *)_prog_data->gprof.tx_interface,
                        "auto"
                ) == MATCH
        ) ? ISTRUE : ISFALSE;
        NEW_SESSION->TARGET_TOTAL = _prog_data->active_project_target_count;
        NEW_SESSION->NEXT_TARGET = _prog_data->active_project_flower->NEXT;
        NEW_SESSION->TX_INTERVAL_NS = 1000000000ULL / (uint64_t)TX_RATE;

        if (NEW_SESSION->TX_INTERVAL_NS == 0) {
                NEW_SESSION->TX_INTERVAL_NS = 1;
        }

        atomic_init(&NEW_SESSION->BUILD_OUTSTANDING, 0U);
        atomic_init(&NEW_SESSION->NEXT_SEQUENCE, 0U);
        atomic_init(&NEW_SESSION->CANCELLING, ISFALSE);
        atomic_init(&NEW_SESSION->FATAL, ISFALSE);

        if (kscan_dispatcher_open(NEW_SESSION) != NORMAL) {
                free(NEW_SESSION);
                return ABNORMAL;
        }

        if (
                kworker_pool_open(
                        &NEW_SESSION->WORKERS,
                        NEW_SESSION->WORKER_COUNT,
                        NEW_SESSION->BATCH_LIMIT
                ) != KWORKER_OK
        ) {
                kscan_dispatcher_close(NEW_SESSION);
                free(NEW_SESSION);
                return ABNORMAL;
        }

        *SESSION = NEW_SESSION;
        return NORMAL;
}

int kscan_session_schedule(
        KSCAN_SESSION * SESSION
) {
        uint64_t NOW_NS;

        if (!SESSION) {
                return ABNORMAL;
        }

        if (
                atomic_load(&SESSION->CANCELLING) == ISTRUE
                || atomic_load(&SESSION->FATAL) == ISTRUE
        ) {
                return ABNORMAL;
        }

        NOW_NS = kscan_monotonic_ns();
        if (NOW_NS == 0) {
                atomic_store(&SESSION->FATAL, ISTRUE);
                return ABNORMAL;
        }

        kscan_drain_events(SESSION);
        kscan_retire_expired(SESSION, NOW_NS);

        if (kscan_refill_targets(SESSION) != NORMAL) {
                return ABNORMAL;
        }

        if (kscan_transmit_ready(SESSION, NOW_NS) != NORMAL) {
                atomic_store(&SESSION->FATAL, ISTRUE);
                return ABNORMAL;
        }

        kscan_drain_events(SESSION);
        return NORMAL;
}

int8_t kscan_session_complete(
        KSCAN_SESSION * SESSION
) {
        if (!SESSION) {
                return ISFALSE;
        }

        return (
                SESSION->NEXT_TARGET == NULL
                && atomic_load(&SESSION->BUILD_OUTSTANDING) == 0
                && kscan_dispatch_ready_count(SESSION) == 0
                && kscan_dispatch_event_count(SESSION) == 0
                && SESSION->PENDING_COUNT == 0
                && SESSION->STAGED_TARGETS == 0
        ) ? ISTRUE : ISFALSE;
}

void kscan_session_cancel(
        KSCAN_SESSION * SESSION
) {
        if (!SESSION) {
                return;
        }

        atomic_store(
                &SESSION->CANCELLING,
                ISTRUE
        );
}

void kscan_session_close(
        KSCAN_SESSION ** SESSION
) {
        KSCAN_SESSION * ACTIVE_SESSION;
        KSCAN_TRANSACTION * TRANSACTION;
        KSCAN_EVENT * EVENT;
        int8_t FAST_CANCEL;

        if (!SESSION || !*SESSION) {
                return;
        }

        ACTIVE_SESSION = *SESSION;
        FAST_CANCEL = (
                atomic_load(&ACTIVE_SESSION->CANCELLING) == ISTRUE
                || atomic_load(&ACTIVE_SESSION->FATAL) == ISTRUE
        ) ? ISTRUE : ISFALSE;

        atomic_store(&ACTIVE_SESSION->CANCELLING, ISTRUE);

        if (FAST_CANCEL == ISTRUE) {
                kworker_pool_cancel(
                        &ACTIVE_SESSION->WORKERS
                );
        } else {
                kworker_pool_close(
                        &ACTIVE_SESSION->WORKERS
                );
        }

        while (
                (TRANSACTION = kscan_dispatch_ready_pop(ACTIVE_SESSION)) != NULL
        ) {
                kscan_transaction_release(
                        ACTIVE_SESSION,
                        TRANSACTION
                );
        }

        while (ACTIVE_SESSION->PENDING_HEAD) {
                TRANSACTION = ACTIVE_SESSION->PENDING_HEAD;
                kscan_pending_remove(
                        ACTIVE_SESSION,
                        TRANSACTION
                );
                kscan_transaction_release(
                        ACTIVE_SESSION,
                        TRANSACTION
                );
        }

        while (
                (EVENT = kscan_dispatch_event_pop(ACTIVE_SESSION)) != NULL
        ) {
                free(EVENT);
        }

        if (ACTIVE_SESSION->TX_AUTO_LOCKED == ISTRUE) {
                ACTIVE_SESSION->PROG_DATA->nosix_status =
                        kwire_scan_restore_auto_interface(
                                ACTIVE_SESSION->PROG_DATA
                        );
                ACTIVE_SESSION->TX_AUTO_LOCKED = ISFALSE;
        }

        kscan_dispatcher_close(ACTIVE_SESSION);
        free(ACTIVE_SESSION);
        *SESSION = NULL;
}

int kscan_session_run_tx(
        _carry_forward * _prog_data
) {
        KSCAN_SESSION * SESSION = NULL;
        struct timespec IDLE_WAIT;
        uint64_t NOW_NS;
        uint64_t WAIT_NS;
        uint64_t DELTA_NS;
        int RESULT = NORMAL;

        if (
                kscan_session_open(
                        _prog_data,
                        &SESSION
                ) != NORMAL
        ) {
                return ABNORMAL;
        }

        while (
                kscan_session_complete(SESSION) != ISTRUE
        ) {
                if (kscan_session_schedule(SESSION) != NORMAL) {
                        RESULT = ABNORMAL;
                        kscan_session_cancel(SESSION);
                        break;
                }

                if (kscan_session_complete(SESSION) == ISTRUE) {
                        break;
                }

                NOW_NS = kscan_monotonic_ns();
                if (NOW_NS == 0) {
                        RESULT = ABNORMAL;
                        kscan_session_cancel(SESSION);
                        break;
                }

                WAIT_NS = KSCAN_IDLE_SLEEP_NS;

                if (
                        kscan_dispatch_ready_count(SESSION) > 0
                        && SESSION->PENDING_COUNT < SESSION->PENDING_LIMIT
                        && SESSION->CAPTURE_BLOCKED != ISTRUE
                        && !(
                                SESSION->TX_INTERFACE_AUTO == ISTRUE
                                && SESSION->TX_AUTO_LOCKED != ISTRUE
                                && SESSION->PENDING_COUNT > 0
                        )
                ) {
                        if (SESSION->NEXT_TX_NS <= NOW_NS) {
                                continue;
                        }

                        DELTA_NS = SESSION->NEXT_TX_NS - NOW_NS;
                        if (DELTA_NS < WAIT_NS) {
                                WAIT_NS = DELTA_NS;
                        }
                }

                if (
                        SESSION->PENDING_HEAD
                        && SESSION->PENDING_HEAD->DEADLINE_NS > NOW_NS
                ) {
                        DELTA_NS = SESSION->PENDING_HEAD->DEADLINE_NS - NOW_NS;
                        if (DELTA_NS < WAIT_NS) {
                                WAIT_NS = DELTA_NS;
                        }
                }

                if (WAIT_NS == 0) {
                        continue;
                }

                IDLE_WAIT.tv_sec = 0;
                IDLE_WAIT.tv_nsec = (long)WAIT_NS;
                nanosleep(&IDLE_WAIT, NULL);
        }

        kscan_session_close(&SESSION);
        return RESULT;
}
