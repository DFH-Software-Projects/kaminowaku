// Copyright 2026 Jamison A. Drapeau
#ifndef __BOOK_SESSION__H
#define __BOOK_SESSION__H

#include "book_core.h"
#include <nosix_datagram.h>
#include <pthread.h>
#include <stdatomic.h>

#define BOOK_SESSION_HARD_TIMEOUT_MS 120000U
#define BOOK_PCAP_LINKTYPE_ETHERNET 1U
#define BOOK_PCAP_LINKTYPE_RAW_IPV4 101U

typedef struct BOOK_OUTPUT_STAGE BOOK_OUTPUT_STAGE;

// @@ Book transport ownership
typedef enum {
        BOOK_TRANSPORT_NONE = 0,
        BOOK_TRANSPORT_TCP  = 1,
        BOOK_TRANSPORT_UDP  = 2
} book_transport_t;

// @@ Book session lifecycle
typedef enum {
        BOOK_SESSION_RESET          = 0,
        BOOK_SESSION_CREATED        = 1,
        BOOK_SESSION_CAPTURE_ACTIVE = 2,
        BOOK_SESSION_RUNTIME        = 3,
        BOOK_SESSION_CLOSING        = 4,
        BOOK_SESSION_FINALIZED      = 5
} book_session_state_t;

// @@ Stable book termination states
typedef enum {
        BOOK_TERM_NONE                = 0,
        BOOK_TERM_COMPLETE            = 1,
        BOOK_TERM_BAILED              = 2,
        BOOK_TERM_RUNTIME_ERROR       = 3,
        BOOK_TERM_TRANSPORT_ERROR     = 4,
        BOOK_TERM_LIMIT_ERROR         = 5,
        BOOK_TERM_INTERRUPTED         = 6,
        BOOK_TERM_CAPTURE_SETUP_ERROR = 7,
        BOOK_TERM_OUTPUT_COMMIT_ERROR = 8
} book_termination_t;

// @@ One invocation owns one managed communication stack
typedef struct BOOK_SESSION {
        uint64_t                session_id;
        unsigned char           book_name[BOOK_NAME_BLOCK];
        unsigned char           tid[TID_BLOCK];

        _carry_forward *        prog_data;
        FLOWER *                target;

        nosix_address_family_t  address_family;
        book_transport_t        transport;
        uint16_t                destination_port;
        uint16_t                local_port;
        nosix_stream_t *        stream;
        nosix_datagram_t *      datagram;

        book_session_state_t    state;
        book_termination_t      termination;

        int8_t                  capture_active;
        int                     pcap_fd;
        uint32_t                pcap_linktype;
        uint64_t                pcap_packets;
        uint64_t                pcap_bytes;
        pthread_t               capture_thread;
        int8_t                  capture_thread_running;
        atomic_int              capture_thread_stop;
        atomic_int              capture_thread_error;
        unsigned char           pcap_path[MAX_PATH];
        uint64_t                started_ns;
        uint64_t                execution_deadline_ns;  // CLOCK_MONOTONIC hard session budget
        uint64_t                ended_ns;
        uint64_t                network_actions;
        uint64_t                tx_bytes;
        uint64_t                rx_bytes;

        // Staging survives executor teardown so output is committed only after
        // transport cleanup and PCAP finalization.
        BOOK_OUTPUT_STAGE *     output_stage;
} BOOK_SESSION;

int books_session_prepare(
        BOOK_SESSION * session,
        _carry_forward * _prog_data,
        const unsigned char * book_name,
        FLOWER * target,
        const char * target_directory
);

int books_session_network_allowed(const BOOK_SESSION * session);
int32_t books_session_remaining_ms(const BOOK_SESSION * session);
void books_session_set_termination(BOOK_SESSION * session, book_termination_t termination);
void books_session_note_network_action(BOOK_SESSION * session);

nosix_status_t books_session_open_tcp(
        BOOK_SESSION * session,
        uint16_t port,
        int32_t timeout_ms
);

nosix_status_t books_session_open_udp(
        BOOK_SESSION * session,
        uint16_t port,
        int32_t timeout_ms
);

nosix_status_t books_session_payload_write(
        BOOK_SESSION * session,
        const uint8_t * data,
        size_t length,
        size_t * written,
        int32_t timeout_ms
);

nosix_status_t books_session_payload_read(
        BOOK_SESSION * session,
        uint8_t * data,
        size_t capacity,
        size_t * received,
        int32_t timeout_ms
);

nosix_status_t books_session_stream_write(
        BOOK_SESSION * session,
        const uint8_t * data,
        size_t length,
        size_t * written,
        int32_t timeout_ms
);

nosix_status_t books_session_stream_read(
        BOOK_SESSION * session,
        uint8_t * data,
        size_t capacity,
        size_t * received,
        int32_t timeout_ms
);

int books_session_capture_drain(
        BOOK_SESSION * session,
        int32_t first_wait_ms
);

void books_session_finalize(BOOK_SESSION * session);
const char * books_termination_name(book_termination_t termination);

#endif
