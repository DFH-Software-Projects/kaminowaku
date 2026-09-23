// Copyright 2026 Jamison A. Drapeau
#ifndef __KPORTSCAN_INTERNAL__H
#define __KPORTSCAN_INTERNAL__H

#include "data.h"
#include "kportscan.h"
#include "kportspec.h"
#include "kwire.h"

#include <stddef.h>
#include <stdint.h>

#if MAX_PORTS != KPORTSPEC_MAX_PORTS
#error "Kaminowaku port bounds must match the shared port-spec parser."
#endif

#define KPORTSCAN_BITMAP_BYTES       KPORTSPEC_BITMAP_BYTES
#define KPORTSCAN_WINDOW_HARD        4096U
#define KPORTSCAN_TCP_HEADER_LENGTH    20U
#define KPORTSCAN_UDP_HEADER_LENGTH     8U
#define KPORTSCAN_TCP_SOURCE_BASE    41000U
#define KPORTSCAN_UDP_SOURCE_BASE    43000U
#define KPORTSCAN_STORE_VERSION          1U

typedef struct KPORTSCAN_PLAN {
        uint8_t TCP_PORTS[KPORTSCAN_BITMAP_BYTES];
        uint8_t UDP_PORTS[KPORTSCAN_BITMAP_BYTES];
        uint32_t TCP_COUNT;
        uint32_t UDP_COUNT;
        int8_t FULL;
} KPORTSCAN_PLAN;

typedef struct KPORTSCAN_SUMMARY {
        uint64_t SCANNED;
        uint64_t OPEN;
        uint64_t CLOSED;
        uint64_t FILTERED;
        uint64_t OPEN_FILTERED;
        uint64_t ERROR;
} KPORTSCAN_SUMMARY;

typedef struct KPORTSCAN_PENDING {
        uint64_t PCAP_TIMESTAMP_NS;
        uint32_t SEQUENCE;
        uint16_t PORT;
        uint16_t SOURCE_PORT;
        int8_t SENT;
        int8_t DONE;
} KPORTSCAN_PENDING;

int kportscan_tcp(
        _carry_forward * _prog_data,
        const unsigned char * TID,
        TARGET * PETAL,
        const char * ADDRESS,
        nosix_address_family_t FAMILY,
        const KPORTSCAN_PLAN * PLAN,
        KPORTSCAN_SUMMARY * SUMMARY,
        int8_t PRINT_NEGATIVE
);

int kportscan_udp(
        _carry_forward * _prog_data,
        const unsigned char * TID,
        const char * ADDRESS,
        nosix_address_family_t FAMILY,
        const KPORTSCAN_PLAN * PLAN,
        KPORTSCAN_SUMMARY * SUMMARY,
        int8_t PRINT_NEGATIVE
);

int8_t kportscan_plan_has_port(
        const uint8_t * BITMAP,
        uint16_t PORT
);

uint32_t kportscan_window_limit(
        const _carry_forward * _prog_data
);

uint64_t kportscan_monotonic_ns(void);

void kportscan_rate_wait(
        const _carry_forward * _prog_data,
        uint64_t * NEXT_TX_NS
);

nosix_status_t kportscan_write_probe(
        _carry_forward * _prog_data,
        KPCAP * PCAP,
        const nosix_tx_packet_t * PACKET,
        uint64_t * NEXT_TX_NS,
        size_t * FRAME_LENGTH
);

int kportscan_pcap_open(
        _carry_forward * _prog_data,
        const unsigned char * TID,
        KPORTSCAN_PROTOCOL PROTOCOL,
        nosix_address_family_t FAMILY,
        KPORTSCAN_PENDING * PENDING,
        KPCAP * PCAP
);

int kportscan_pcap_accept(
        _carry_forward * _prog_data,
        const unsigned char * TID,
        KPORTSCAN_PROTOCOL PROTOCOL,
        nosix_address_family_t FAMILY,
        const KPORTSCAN_PENDING * PENDING,
        const nosix_capture_t * CAPTURE
);

int kportscan_capture_l3(
        const nosix_capture_t * CAPTURE,
        nosix_address_family_t FAMILY,
        const uint8_t ** IP,
        size_t * IP_LENGTH
);

int kportscan_address_matches(
        const uint8_t * BYTES,
        const char * ADDRESS,
        nosix_address_family_t FAMILY
);

int kportscan_store_result(
        _carry_forward * _prog_data,
        const unsigned char * TID,
        const KPORTSCAN_RESULT * RESULT
);

void kportscan_summary_add(
        KPORTSCAN_SUMMARY * SUMMARY,
        KPORTSCAN_STATE STATE
);

const char * kportscan_nosix_status_string(nosix_status_t STATUS);

void kportscan_emit_result(
        const char * ADDRESS,
        nosix_address_family_t FAMILY,
        const KPORTSCAN_RESULT * RESULT,
        int8_t PRINT_NEGATIVE
);

#endif
