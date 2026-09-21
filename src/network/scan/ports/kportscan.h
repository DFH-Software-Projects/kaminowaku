// Copyright 2026 Jamison A. Drapeau
#ifndef __KPORTSCAN__H
#define __KPORTSCAN__H

#include "data.h"

typedef enum KPORTSCAN_PROTOCOL {
        KPORTSCAN_PROTOCOL_TCP = NOSIX_IPPROTO_TCP,
        KPORTSCAN_PROTOCOL_UDP = NOSIX_IPPROTO_UDP
} KPORTSCAN_PROTOCOL;

typedef enum KPORTSCAN_STATE {
        KPORTSCAN_STATE_UNKNOWN = 0,
        KPORTSCAN_STATE_OPEN,
        KPORTSCAN_STATE_CLOSED,
        KPORTSCAN_STATE_FILTERED,
        KPORTSCAN_STATE_OPEN_FILTERED,
        KPORTSCAN_STATE_ERROR
} KPORTSCAN_STATE;

typedef enum KPORTSCAN_EVIDENCE {
        KPORTSCAN_EVIDENCE_NONE = 0,
        KPORTSCAN_EVIDENCE_TCP_SYN_ACK,
        KPORTSCAN_EVIDENCE_TCP_RST,
        KPORTSCAN_EVIDENCE_UDP_REPLY,
        KPORTSCAN_EVIDENCE_ICMP_PORT_UNREACHABLE,
        KPORTSCAN_EVIDENCE_ICMP_FILTERED,
        KPORTSCAN_EVIDENCE_TIMEOUT,
        KPORTSCAN_EVIDENCE_NETWORK_ERROR
} KPORTSCAN_EVIDENCE;

typedef struct KPORTSCAN_RESULT {
        uint64_t TIMESTAMP_NS;
        uint32_t RX_BYTES;
        uint16_t PORT;
        uint8_t PROTOCOL;
        uint8_t ADDRESS_FAMILY;
        uint8_t STATE;
        uint8_t EVIDENCE;
} KPORTSCAN_RESULT;

const char * kportscan_state_string(KPORTSCAN_STATE STATE);
const char * kportscan_evidence_string(KPORTSCAN_EVIDENCE EVIDENCE);

void kportscan_print_usage(void);
void kportscan_run(_carry_forward * _prog_data);

#endif
