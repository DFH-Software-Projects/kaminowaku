// Copyright 2026 Jamison A. Drapeau
#include "kportscan_internal.h"
#include "kscan.h"
#include "kui.h"

#include <arpa/inet.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#define KPORTSCAN_TCP_FLAG_SYN 0x02U
#define KPORTSCAN_TCP_FLAG_RST 0x04U
#define KPORTSCAN_TCP_FLAG_ACK 0x10U

static uint16_t kportscan_tcp_source_port(uint16_t PORT) {
        return (uint16_t)(KPORTSCAN_TCP_SOURCE_BASE + (PORT % 20000U));
}

static uint32_t kportscan_tcp_sequence(uint16_t PORT) {
        return (
                0x4b570000U
                ^ ((uint32_t)PORT << 8)
                ^ (uint32_t)(kportscan_monotonic_ns() & 0xffffU)
        );
}

static int kportscan_tcp_build(
        nosix_tx_packet_t * PACKET,
        uint8_t * UPPER,
        size_t UPPER_SIZE,
        const char * ADDRESS,
        nosix_address_family_t FAMILY,
        uint16_t PORT,
        uint16_t SOURCE_PORT,
        uint32_t SEQUENCE
) {
        int AF;

        if (
                !PACKET
                || !UPPER
                || UPPER_SIZE < KPORTSCAN_TCP_HEADER_LENGTH
                || !ADDRESS
        ) {
                return ABNORMAL;
        }

        memset(PACKET, 0x00, sizeof(*PACKET));
        memset(UPPER, 0x00, UPPER_SIZE);

        UPPER[0] = (uint8_t)(SOURCE_PORT >> 8);
        UPPER[1] = (uint8_t)SOURCE_PORT;
        UPPER[2] = (uint8_t)(PORT >> 8);
        UPPER[3] = (uint8_t)PORT;

        UPPER[4] = (uint8_t)(SEQUENCE >> 24);
        UPPER[5] = (uint8_t)(SEQUENCE >> 16);
        UPPER[6] = (uint8_t)(SEQUENCE >> 8);
        UPPER[7] = (uint8_t)SEQUENCE;

        UPPER[12] = 0x50;
        UPPER[13] = KPORTSCAN_TCP_FLAG_SYN;
        UPPER[14] = 0xfa;
        UPPER[15] = 0xf0;

        PACKET->destination.family = FAMILY;
        AF = FAMILY == NOSIX_ADDRESS_IPV4 ? AF_INET : AF_INET6;

        if (
                inet_pton(
                        AF,
                        ADDRESS,
                        FAMILY == NOSIX_ADDRESS_IPV4
                                ? (void *)PACKET->destination.bytes.ipv4
                                : (void *)PACKET->destination.bytes.ipv6
                ) != 1
        ) {
                return ABNORMAL;
        }

        PACKET->ip_protocol = NOSIX_IPPROTO_TCP;
        PACKET->upper_layer = UPPER;
        PACKET->upper_layer_length = KPORTSCAN_TCP_HEADER_LENGTH;
        PACKET->flags = 0;
        return NORMAL;
}

static KPORTSCAN_PENDING * kportscan_tcp_find_pending(
        KPORTSCAN_PENDING * PENDING,
        uint32_t COUNT,
        uint16_t TARGET_PORT,
        uint16_t LOCAL_PORT
) {
        if (!PENDING) {
                return NULL;
        }

        for (uint32_t INDEX = 0; INDEX < COUNT; INDEX++) {
                if (
                        PENDING[INDEX].SENT == ISTRUE
                        && PENDING[INDEX].DONE != ISTRUE
                        && PENDING[INDEX].PORT == TARGET_PORT
                        && PENDING[INDEX].SOURCE_PORT == LOCAL_PORT
                ) {
                        return &PENDING[INDEX];
                }
        }

        return NULL;
}

static void kportscan_tcp_finalize(
        _carry_forward * _prog_data,
        const unsigned char * TID,
        TARGET * PETAL,
        const char * ADDRESS,
        nosix_address_family_t FAMILY,
        KPORTSCAN_PENDING * PENDING,
        KPORTSCAN_SUMMARY * SUMMARY,
        KPORTSCAN_STATE STATE,
        KPORTSCAN_EVIDENCE EVIDENCE,
        size_t RX_BYTES,
        int8_t PRINT_NEGATIVE
) {
        KPORTSCAN_RESULT RESULT;

        if (
                !_prog_data
                || !TID
                || !PETAL
                || !ADDRESS
                || !PENDING
                || !SUMMARY
                || PENDING->DONE == ISTRUE
        ) {
                return;
        }

        memset(&RESULT, 0x00, sizeof(RESULT));
        RESULT.TIMESTAMP_NS = kscan_now_ns();
        RESULT.RX_BYTES = RX_BYTES > UINT32_MAX
                ? UINT32_MAX
                : (uint32_t)RX_BYTES;
        RESULT.PORT = PENDING->PORT;
        RESULT.PROTOCOL = KPORTSCAN_PROTOCOL_TCP;
        RESULT.ADDRESS_FAMILY = (uint8_t)FAMILY;
        RESULT.STATE = (uint8_t)STATE;
        RESULT.EVIDENCE = (uint8_t)EVIDENCE;

        if (STATE == KPORTSCAN_STATE_OPEN) {
                PETAL->PORTS[PENDING->PORT] = PORT_OPEN;
        } else if (
                STATE == KPORTSCAN_STATE_CLOSED
                && PETAL->PORTS[PENDING->PORT] != PORT_OPEN
        ) {
                PETAL->PORTS[PENDING->PORT] = PORT_CLOSED;
        }

        if (kportscan_store_result(_prog_data, TID, &RESULT) != NORMAL) {
                kui_add_line_and_render(
                        NOTICE_ERROR
                        "Failed to persist TCP/%u result for target "
                        ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET ".",
                        RESULT.PORT,
                        TID
                );
        }

        kportscan_summary_add(SUMMARY, STATE);
        kportscan_emit_result(
                ADDRESS,
                FAMILY,
                &RESULT,
                PRINT_NEGATIVE
        );
        PENDING->DONE = ISTRUE;
}

static int kportscan_tcp_direct_reply(
        _carry_forward * _prog_data,
        const unsigned char * TID,
        TARGET * PETAL,
        const char * ADDRESS,
        nosix_address_family_t FAMILY,
        const nosix_capture_t * CAPTURE,
        const uint8_t * IP,
        size_t IP_LENGTH,
        KPORTSCAN_PENDING * PENDING,
        uint32_t PENDING_COUNT,
        KPORTSCAN_SUMMARY * SUMMARY,
        int8_t PRINT_NEGATIVE
) {
        const uint8_t * TCP;
        size_t TCP_LENGTH;
        uint16_t SOURCE_PORT;
        uint16_t DESTINATION_PORT;
        uint32_t ACKNOWLEDGEMENT;
        uint8_t FLAGS;
        KPORTSCAN_PENDING * MATCHED;

        if (FAMILY == NOSIX_ADDRESS_IPV4) {
                size_t IHL;

                if (
                        IP_LENGTH < 20
                        || (IP[0] >> 4) != 4
                        || IP[9] != NOSIX_IPPROTO_TCP
                ) {
                        return ISFALSE;
                }

                IHL = (size_t)(IP[0] & 0x0fU) * 4U;
                if (
                        IHL < 20
                        || IP_LENGTH < IHL + KPORTSCAN_TCP_HEADER_LENGTH
                        || kportscan_address_matches(
                                IP + 12,
                                ADDRESS,
                                FAMILY
                        ) != ISTRUE
                ) {
                        return ISFALSE;
                }

                TCP = IP + IHL;
                TCP_LENGTH = IP_LENGTH - IHL;
        } else {
                if (
                        IP_LENGTH < 40 + KPORTSCAN_TCP_HEADER_LENGTH
                        || (IP[0] >> 4) != 6
                        || IP[6] != NOSIX_IPPROTO_TCP
                        || kportscan_address_matches(
                                IP + 8,
                                ADDRESS,
                                FAMILY
                        ) != ISTRUE
                ) {
                        return ISFALSE;
                }

                TCP = IP + 40;
                TCP_LENGTH = IP_LENGTH - 40;
        }

        if (TCP_LENGTH < KPORTSCAN_TCP_HEADER_LENGTH) {
                return ISFALSE;
        }

        SOURCE_PORT = (uint16_t)(
                ((uint16_t)TCP[0] << 8)
                | TCP[1]
        );
        DESTINATION_PORT = (uint16_t)(
                ((uint16_t)TCP[2] << 8)
                | TCP[3]
        );

        MATCHED = kportscan_tcp_find_pending(
                PENDING,
                PENDING_COUNT,
                SOURCE_PORT,
                DESTINATION_PORT
        );

        if (!MATCHED) {
                return ISFALSE;
        }

        FLAGS = TCP[13];
        ACKNOWLEDGEMENT = (
                ((uint32_t)TCP[8] << 24)
                | ((uint32_t)TCP[9] << 16)
                | ((uint32_t)TCP[10] << 8)
                | (uint32_t)TCP[11]
        );

        if (
                (FLAGS & (KPORTSCAN_TCP_FLAG_SYN | KPORTSCAN_TCP_FLAG_ACK))
                        == (KPORTSCAN_TCP_FLAG_SYN | KPORTSCAN_TCP_FLAG_ACK)
                && ACKNOWLEDGEMENT == MATCHED->SEQUENCE + 1U
        ) {
                (void)kportscan_pcap_accept(
                _prog_data,
                TID,
                KPORTSCAN_PROTOCOL_TCP,
                FAMILY,
                MATCHED,
                CAPTURE
        );
                kportscan_tcp_finalize(
                        _prog_data,
                        TID,
                        PETAL,
                        ADDRESS,
                        FAMILY,
                        MATCHED,
                        SUMMARY,
                        KPORTSCAN_STATE_OPEN,
                        KPORTSCAN_EVIDENCE_TCP_SYN_ACK,
                        CAPTURE->wire_length
                                ? CAPTURE->wire_length
                                : CAPTURE->frame.length,
                        PRINT_NEGATIVE
                );
                return ISTRUE;
        }

        if (FLAGS & KPORTSCAN_TCP_FLAG_RST) {
                (void)kportscan_pcap_accept(
                _prog_data,
                TID,
                KPORTSCAN_PROTOCOL_TCP,
                FAMILY,
                MATCHED,
                CAPTURE
        );
                kportscan_tcp_finalize(
                        _prog_data,
                        TID,
                        PETAL,
                        ADDRESS,
                        FAMILY,
                        MATCHED,
                        SUMMARY,
                        KPORTSCAN_STATE_CLOSED,
                        KPORTSCAN_EVIDENCE_TCP_RST,
                        CAPTURE->wire_length
                                ? CAPTURE->wire_length
                                : CAPTURE->frame.length,
                        PRINT_NEGATIVE
                );
                return ISTRUE;
        }

        return ISFALSE;
}

static int kportscan_tcp_icmpv4(
        _carry_forward * _prog_data,
        const unsigned char * TID,
        TARGET * PETAL,
        const char * ADDRESS,
        const nosix_capture_t * CAPTURE,
        const uint8_t * IP,
        size_t IP_LENGTH,
        KPORTSCAN_PENDING * PENDING,
        uint32_t PENDING_COUNT,
        KPORTSCAN_SUMMARY * SUMMARY,
        int8_t PRINT_NEGATIVE
) {
        size_t OUTER_IHL;
        const uint8_t * ICMP;
        size_t ICMP_LENGTH;
        const uint8_t * INNER_IP;
        size_t INNER_IHL;
        const uint8_t * INNER_TCP;
        uint16_t SOURCE_PORT;
        uint16_t DESTINATION_PORT;
        KPORTSCAN_PENDING * MATCHED;

        if (
                IP_LENGTH < 20
                || (IP[0] >> 4) != 4
                || IP[9] != NOSIX_IPPROTO_ICMP
        ) {
                return ISFALSE;
        }

        OUTER_IHL = (size_t)(IP[0] & 0x0fU) * 4U;
        if (OUTER_IHL < 20 || IP_LENGTH < OUTER_IHL + 8 + 20 + 8) {
                return ISFALSE;
        }

        ICMP = IP + OUTER_IHL;
        ICMP_LENGTH = IP_LENGTH - OUTER_IHL;

        if (ICMP[0] != 3 || ICMP_LENGTH < 8 + 20 + 8) {
                return ISFALSE;
        }

        INNER_IP = ICMP + 8;
        if ((INNER_IP[0] >> 4) != 4 || INNER_IP[9] != NOSIX_IPPROTO_TCP) {
                return ISFALSE;
        }

        INNER_IHL = (size_t)(INNER_IP[0] & 0x0fU) * 4U;
        if (
                INNER_IHL < 20
                || ICMP_LENGTH < 8 + INNER_IHL + 8
                || kportscan_address_matches(
                        INNER_IP + 16,
                        ADDRESS,
                        NOSIX_ADDRESS_IPV4
                ) != ISTRUE
        ) {
                return ISFALSE;
        }

        INNER_TCP = INNER_IP + INNER_IHL;
        SOURCE_PORT = (uint16_t)(
                ((uint16_t)INNER_TCP[0] << 8)
                | INNER_TCP[1]
        );
        DESTINATION_PORT = (uint16_t)(
                ((uint16_t)INNER_TCP[2] << 8)
                | INNER_TCP[3]
        );

        MATCHED = kportscan_tcp_find_pending(
                PENDING,
                PENDING_COUNT,
                DESTINATION_PORT,
                SOURCE_PORT
        );

        if (!MATCHED) {
                return ISFALSE;
        }

        (void)kportscan_pcap_accept(
                _prog_data,
                TID,
                KPORTSCAN_PROTOCOL_TCP,
                NOSIX_ADDRESS_IPV4,
                MATCHED,
                CAPTURE
        );
        kportscan_tcp_finalize(
                _prog_data,
                TID,
                PETAL,
                ADDRESS,
                NOSIX_ADDRESS_IPV4,
                MATCHED,
                SUMMARY,
                KPORTSCAN_STATE_FILTERED,
                KPORTSCAN_EVIDENCE_ICMP_FILTERED,
                CAPTURE->wire_length
                        ? CAPTURE->wire_length
                        : CAPTURE->frame.length,
                PRINT_NEGATIVE
        );
        return ISTRUE;
}

static int kportscan_tcp_icmpv6(
        _carry_forward * _prog_data,
        const unsigned char * TID,
        TARGET * PETAL,
        const char * ADDRESS,
        const nosix_capture_t * CAPTURE,
        const uint8_t * IP,
        size_t IP_LENGTH,
        KPORTSCAN_PENDING * PENDING,
        uint32_t PENDING_COUNT,
        KPORTSCAN_SUMMARY * SUMMARY,
        int8_t PRINT_NEGATIVE
) {
        const uint8_t * ICMP6;
        const uint8_t * INNER_IP;
        const uint8_t * INNER_TCP;
        uint16_t SOURCE_PORT;
        uint16_t DESTINATION_PORT;
        KPORTSCAN_PENDING * MATCHED;

        if (
                IP_LENGTH < 40 + 8 + 40 + 8
                || (IP[0] >> 4) != 6
                || IP[6] != NOSIX_IPPROTO_ICMPV6
        ) {
                return ISFALSE;
        }

        ICMP6 = IP + 40;
        if (ICMP6[0] != 1) {
                return ISFALSE;
        }

        INNER_IP = ICMP6 + 8;
        if (
                (INNER_IP[0] >> 4) != 6
                || INNER_IP[6] != NOSIX_IPPROTO_TCP
                || kportscan_address_matches(
                        INNER_IP + 24,
                        ADDRESS,
                        NOSIX_ADDRESS_IPV6
                ) != ISTRUE
        ) {
                return ISFALSE;
        }

        INNER_TCP = INNER_IP + 40;
        SOURCE_PORT = (uint16_t)(
                ((uint16_t)INNER_TCP[0] << 8)
                | INNER_TCP[1]
        );
        DESTINATION_PORT = (uint16_t)(
                ((uint16_t)INNER_TCP[2] << 8)
                | INNER_TCP[3]
        );

        MATCHED = kportscan_tcp_find_pending(
                PENDING,
                PENDING_COUNT,
                DESTINATION_PORT,
                SOURCE_PORT
        );

        if (!MATCHED) {
                return ISFALSE;
        }

        (void)kportscan_pcap_accept(
                _prog_data,
                TID,
                KPORTSCAN_PROTOCOL_TCP,
                NOSIX_ADDRESS_IPV6,
                MATCHED,
                CAPTURE
        );
        kportscan_tcp_finalize(
                _prog_data,
                TID,
                PETAL,
                ADDRESS,
                NOSIX_ADDRESS_IPV6,
                MATCHED,
                SUMMARY,
                KPORTSCAN_STATE_FILTERED,
                KPORTSCAN_EVIDENCE_ICMP_FILTERED,
                CAPTURE->wire_length
                        ? CAPTURE->wire_length
                        : CAPTURE->frame.length,
                PRINT_NEGATIVE
        );
        return ISTRUE;
}

static int kportscan_tcp_receive_batch(
        _carry_forward * _prog_data,
        const unsigned char * TID,
        TARGET * PETAL,
        const char * ADDRESS,
        nosix_address_family_t FAMILY,
        KPORTSCAN_PENDING * PENDING,
        uint32_t PENDING_COUNT,
        KPORTSCAN_SUMMARY * SUMMARY,
        int8_t PRINT_NEGATIVE
) {
        uint8_t CATCH[OUT_BLOCK];
        nosix_capture_t CAPTURE;
        nosix_status_t STATUS;
        KWIRE_DEADLINE DEADLINE;

        if (kwire_deadline_start(&DEADLINE, _prog_data->gprof.rx_timeout_ms) != NORMAL) {
                return ABNORMAL;
        }

        memset(CATCH, 0x00, sizeof(CATCH));
        memset(&CAPTURE, 0x00, sizeof(CAPTURE));
        CAPTURE.frame.data = CATCH;
        CAPTURE.frame.capacity = sizeof(CATCH);

        for (;;) {
                uint32_t OUTSTANDING = 0;
                for (uint32_t INDEX = 0; INDEX < PENDING_COUNT; INDEX++) {
                        if (PENDING[INDEX].SENT == ISTRUE && PENDING[INDEX].DONE != ISTRUE) {
                                OUTSTANDING++;
                        }
                }
                if (OUTSTANDING == 0) return NORMAL;
                const uint8_t * IP = NULL;
                size_t IP_LENGTH = 0;

                memset(CATCH, 0x00, sizeof(CATCH));
                CAPTURE.frame.length = 0;
                CAPTURE.wire_length = 0;
                CAPTURE.timestamp_ns = 0;
                CAPTURE.interface_index = 0;
                CAPTURE.flags = 0;

                STATUS = kwire_scan_read_until(_prog_data, &CAPTURE, &DEADLINE);

                if (STATUS == NOSIX_TIMEOUT) {
                        return NORMAL;
                }

                if (STATUS != NOSIX_OK && STATUS != NOSIX_TRUNCATED) {
                        kui_add_line_and_render(
                                NOTICE_ERROR
                                "TCP capture failed: " ANSI_COLOR_CYAN
                                "%s" ANSI_COLOR_RESET " (%d).",
                                kportscan_nosix_status_string(STATUS),
                                STATUS
                        );
                        return ABNORMAL;
                }

                if (
                        kportscan_capture_l3(
                                &CAPTURE,
                                FAMILY,
                                &IP,
                                &IP_LENGTH
                        ) != NORMAL
                ) {
                        continue;
                }

                if (
                        kportscan_tcp_direct_reply(
                                _prog_data,
                                TID,
                                PETAL,
                                ADDRESS,
                                FAMILY,
                                &CAPTURE,
                                IP,
                                IP_LENGTH,
                                PENDING,
                                PENDING_COUNT,
                                SUMMARY,
                                PRINT_NEGATIVE
                        ) == ISTRUE
                ) {
                        continue;
                }

                if (FAMILY == NOSIX_ADDRESS_IPV4) {
                        (void)kportscan_tcp_icmpv4(
                                _prog_data,
                                TID,
                                PETAL,
                                ADDRESS,
                                &CAPTURE,
                                IP,
                                IP_LENGTH,
                                PENDING,
                                PENDING_COUNT,
                                SUMMARY,
                                PRINT_NEGATIVE
                        );
                } else {
                        (void)kportscan_tcp_icmpv6(
                                _prog_data,
                                TID,
                                PETAL,
                                ADDRESS,
                                &CAPTURE,
                                IP,
                                IP_LENGTH,
                                PENDING,
                                PENDING_COUNT,
                                SUMMARY,
                                PRINT_NEGATIVE
                        );
                }
        }
}

int kportscan_tcp(
        _carry_forward * _prog_data,
        const unsigned char * TID,
        TARGET * PETAL,
        const char * ADDRESS,
        nosix_address_family_t FAMILY,
        const KPORTSCAN_PLAN * PLAN,
        KPORTSCAN_SUMMARY * SUMMARY,
        int8_t PRINT_NEGATIVE
) {
        uint32_t WINDOW;
        KPORTSCAN_PENDING * PENDING;
        uint32_t NEXT_PORT = 1;
        uint64_t NEXT_TX_NS = 0;
        int RETURN_STATUS = NORMAL;

        if (
                !_prog_data
                || !TID
                || !PETAL
                || !ADDRESS
                || !PLAN
                || !SUMMARY
                || PLAN->TCP_COUNT == 0
        ) {
                return ABNORMAL;
        }

        WINDOW = kportscan_window_limit(_prog_data);
        PENDING = calloc(WINDOW, sizeof(*PENDING));
        if (!PENDING) {
                return ABNORMAL;
        }

        while (NEXT_PORT < MAX_PORTS) {
                uint32_t COUNT = 0;
                nosix_status_t RESET_STATUS;
                int8_t ROUTE_FAILURE = ISFALSE;

                memset(PENDING, 0x00, WINDOW * sizeof(*PENDING));

                while (NEXT_PORT < MAX_PORTS && COUNT < WINDOW) {
                        if (
                                kportscan_plan_has_port(
                                        PLAN->TCP_PORTS,
                                        (uint16_t)NEXT_PORT
                                ) == ISTRUE
                        ) {
                                PENDING[COUNT].PORT = (uint16_t)NEXT_PORT;
                                PENDING[COUNT].SOURCE_PORT = kportscan_tcp_source_port(
                                        (uint16_t)NEXT_PORT
                                );
                                PENDING[COUNT].SEQUENCE = kportscan_tcp_sequence(
                                        (uint16_t)NEXT_PORT
                                );
                                COUNT++;
                        }
                        NEXT_PORT++;
                }

                if (COUNT == 0) {
                        continue;
                }

                RESET_STATUS = nosix_capture_reset(_prog_data->nosix_net);
                if (RESET_STATUS != NOSIX_OK) {
                        kui_add_line_and_render(
                                NOTICE_ERROR
                                "TCP capture reset failed: " ANSI_COLOR_CYAN
                                "%s" ANSI_COLOR_RESET " (%d).",
                                kportscan_nosix_status_string(RESET_STATUS),
                                RESET_STATUS
                        );
                        RETURN_STATUS = ABNORMAL;
                        break;
                }

                for (uint32_t INDEX = 0; INDEX < COUNT; INDEX++) {
                        nosix_tx_packet_t PACKET;
                        uint8_t UPPER[KPORTSCAN_TCP_HEADER_LENGTH];
                        nosix_status_t STATUS;
                        KPCAP PORT_PCAP;

                        memset(&PORT_PCAP, 0x00, sizeof(PORT_PCAP));

                        if (
                                kportscan_tcp_build(
                                        &PACKET,
                                        UPPER,
                                        sizeof(UPPER),
                                        ADDRESS,
                                        FAMILY,
                                        PENDING[INDEX].PORT,
                                        PENDING[INDEX].SOURCE_PORT,
                                        PENDING[INDEX].SEQUENCE
                                ) != NORMAL
                        ) {
                                kportscan_tcp_finalize(
                                        _prog_data,
                                        TID,
                                        PETAL,
                                        ADDRESS,
                                        FAMILY,
                                        &PENDING[INDEX],
                                        SUMMARY,
                                        KPORTSCAN_STATE_ERROR,
                                        KPORTSCAN_EVIDENCE_NETWORK_ERROR,
                                        0,
                                        PRINT_NEGATIVE
                                );
                                continue;
                        }

                        if (
                                kportscan_pcap_open(
                                        _prog_data,
                                        TID,
                                        KPORTSCAN_PROTOCOL_TCP,
                                        FAMILY,
                                        &PENDING[INDEX],
                                        &PORT_PCAP
                                ) != NORMAL
                        ) {
                                kportscan_tcp_finalize(
                                        _prog_data,
                                        TID,
                                        PETAL,
                                        ADDRESS,
                                        FAMILY,
                                        &PENDING[INDEX],
                                        SUMMARY,
                                        KPORTSCAN_STATE_ERROR,
                                        KPORTSCAN_EVIDENCE_NETWORK_ERROR,
                                        0,
                                        PRINT_NEGATIVE
                                );
                                continue;
                        }

                        STATUS = kportscan_write_probe(
                                _prog_data,
                                &PORT_PCAP,
                                &PACKET,
                                &NEXT_TX_NS,
                                NULL
                        );
                        kwire_pcap_close(&PORT_PCAP);

                        if (STATUS != NOSIX_OK) {
                                kportscan_tcp_finalize(
                                        _prog_data,
                                        TID,
                                        PETAL,
                                        ADDRESS,
                                        FAMILY,
                                        &PENDING[INDEX],
                                        SUMMARY,
                                        KPORTSCAN_STATE_ERROR,
                                        KPORTSCAN_EVIDENCE_NETWORK_ERROR,
                                        0,
                                        PRINT_NEGATIVE
                                );

                                if (
                                        STATUS == NOSIX_ERR_ROUTE
                                        || STATUS == NOSIX_ERR_NEIGHBOR
                                ) {
                                        kui_add_line_and_render(
                                                NOTICE_ERROR
                                                "TCP scan to " ANSI_COLOR_CYAN
                                                "%s" ANSI_COLOR_RESET
                                                " stopped: %s (%d).",
                                                ADDRESS,
                                                kportscan_nosix_status_string(STATUS),
                                                STATUS
                                        );
                                        ROUTE_FAILURE = ISTRUE;
                                        RETURN_STATUS = ABNORMAL;
                                        break;
                                }
                                continue;
                        }

                        PENDING[INDEX].SENT = ISTRUE;
                }

                if (ROUTE_FAILURE == ISTRUE) {
                        break;
                }

                if (
                        kportscan_tcp_receive_batch(
                                _prog_data,
                                TID,
                                PETAL,
                                ADDRESS,
                                FAMILY,
                                PENDING,
                                COUNT,
                                SUMMARY,
                                PRINT_NEGATIVE
                        ) != NORMAL
                ) {
                        RETURN_STATUS = ABNORMAL;
                }

                for (uint32_t INDEX = 0; INDEX < COUNT; INDEX++) {
                        if (
                                PENDING[INDEX].SENT == ISTRUE
                                && PENDING[INDEX].DONE != ISTRUE
                        ) {
                                kportscan_tcp_finalize(
                                        _prog_data,
                                        TID,
                                        PETAL,
                                        ADDRESS,
                                        FAMILY,
                                        &PENDING[INDEX],
                                        SUMMARY,
                                        KPORTSCAN_STATE_FILTERED,
                                        KPORTSCAN_EVIDENCE_TIMEOUT,
                                        0,
                                        PRINT_NEGATIVE
                                );
                        }
                }
        }

        free(PENDING);
        return RETURN_STATUS;
}
