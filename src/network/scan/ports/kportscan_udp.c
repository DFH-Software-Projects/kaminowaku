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

static uint16_t kportscan_udp_source_port(uint16_t PORT) {
        return (uint16_t)(KPORTSCAN_UDP_SOURCE_BASE + (PORT % 20000U));
}

static int kportscan_udp_build(
        nosix_tx_packet_t * PACKET,
        uint8_t * UPPER,
        size_t UPPER_SIZE,
        const char * ADDRESS,
        nosix_address_family_t FAMILY,
        uint16_t PORT,
        uint16_t SOURCE_PORT
) {
        int AF;

        if (
                !PACKET
                || !UPPER
                || UPPER_SIZE < KPORTSCAN_UDP_HEADER_LENGTH
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
        UPPER[4] = 0x00;
        UPPER[5] = KPORTSCAN_UDP_HEADER_LENGTH;

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

        PACKET->ip_protocol = NOSIX_IPPROTO_UDP;
        PACKET->upper_layer = UPPER;
        PACKET->upper_layer_length = KPORTSCAN_UDP_HEADER_LENGTH;
        PACKET->flags = 0;
        return NORMAL;
}

static KPORTSCAN_PENDING * kportscan_udp_find_pending(
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

static void kportscan_udp_finalize(
        _carry_forward * _prog_data,
        const unsigned char * TID,
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
        RESULT.PROTOCOL = KPORTSCAN_PROTOCOL_UDP;
        RESULT.ADDRESS_FAMILY = (uint8_t)FAMILY;
        RESULT.STATE = (uint8_t)STATE;
        RESULT.EVIDENCE = (uint8_t)EVIDENCE;

        if (kportscan_store_result(_prog_data, TID, &RESULT) != NORMAL) {
                kui_add_line_and_render(
                        NOTICE_ERROR
                        "Failed to persist UDP/%u result for target "
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

static int kportscan_udp_direct_reply(
        _carry_forward * _prog_data,
        const unsigned char * TID,
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
        const uint8_t * UDP;
        uint16_t SOURCE_PORT;
        uint16_t DESTINATION_PORT;
        KPORTSCAN_PENDING * MATCHED;

        if (FAMILY == NOSIX_ADDRESS_IPV4) {
                size_t IHL;

                if (
                        IP_LENGTH < 20
                        || (IP[0] >> 4) != 4
                        || IP[9] != NOSIX_IPPROTO_UDP
                ) {
                        return ISFALSE;
                }

                IHL = (size_t)(IP[0] & 0x0fU) * 4U;
                if (
                        IHL < 20
                        || IP_LENGTH < IHL + KPORTSCAN_UDP_HEADER_LENGTH
                        || kportscan_address_matches(
                                IP + 12,
                                ADDRESS,
                                FAMILY
                        ) != ISTRUE
                ) {
                        return ISFALSE;
                }

                UDP = IP + IHL;
        } else {
                if (
                        IP_LENGTH < 40 + KPORTSCAN_UDP_HEADER_LENGTH
                        || (IP[0] >> 4) != 6
                        || IP[6] != NOSIX_IPPROTO_UDP
                        || kportscan_address_matches(
                                IP + 8,
                                ADDRESS,
                                FAMILY
                        ) != ISTRUE
                ) {
                        return ISFALSE;
                }

                UDP = IP + 40;
        }

        SOURCE_PORT = (uint16_t)(
                ((uint16_t)UDP[0] << 8)
                | UDP[1]
        );
        DESTINATION_PORT = (uint16_t)(
                ((uint16_t)UDP[2] << 8)
                | UDP[3]
        );

        MATCHED = kportscan_udp_find_pending(
                PENDING,
                PENDING_COUNT,
                SOURCE_PORT,
                DESTINATION_PORT
        );

        if (!MATCHED) {
                return ISFALSE;
        }

        (void)kportscan_pcap_accept(
                _prog_data,
                TID,
                KPORTSCAN_PROTOCOL_UDP,
                FAMILY,
                MATCHED,
                CAPTURE
        );
        kportscan_udp_finalize(
                _prog_data,
                TID,
                ADDRESS,
                FAMILY,
                MATCHED,
                SUMMARY,
                KPORTSCAN_STATE_OPEN,
                KPORTSCAN_EVIDENCE_UDP_REPLY,
                CAPTURE->wire_length
                        ? CAPTURE->wire_length
                        : CAPTURE->frame.length,
                PRINT_NEGATIVE
        );
        return ISTRUE;
}

static int kportscan_udp_icmpv4(
        _carry_forward * _prog_data,
        const unsigned char * TID,
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
        const uint8_t * INNER_UDP;
        uint16_t SOURCE_PORT;
        uint16_t DESTINATION_PORT;
        KPORTSCAN_PENDING * MATCHED;
        KPORTSCAN_STATE STATE;
        KPORTSCAN_EVIDENCE EVIDENCE;

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
        if ((INNER_IP[0] >> 4) != 4 || INNER_IP[9] != NOSIX_IPPROTO_UDP) {
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

        INNER_UDP = INNER_IP + INNER_IHL;
        SOURCE_PORT = (uint16_t)(
                ((uint16_t)INNER_UDP[0] << 8)
                | INNER_UDP[1]
        );
        DESTINATION_PORT = (uint16_t)(
                ((uint16_t)INNER_UDP[2] << 8)
                | INNER_UDP[3]
        );

        MATCHED = kportscan_udp_find_pending(
                PENDING,
                PENDING_COUNT,
                DESTINATION_PORT,
                SOURCE_PORT
        );

        if (!MATCHED) {
                return ISFALSE;
        }

        STATE = ICMP[1] == 3
                ? KPORTSCAN_STATE_CLOSED
                : KPORTSCAN_STATE_FILTERED;
        EVIDENCE = ICMP[1] == 3
                ? KPORTSCAN_EVIDENCE_ICMP_PORT_UNREACHABLE
                : KPORTSCAN_EVIDENCE_ICMP_FILTERED;

        (void)kportscan_pcap_accept(
                _prog_data,
                TID,
                KPORTSCAN_PROTOCOL_UDP,
                NOSIX_ADDRESS_IPV4,
                MATCHED,
                CAPTURE
        );
        kportscan_udp_finalize(
                _prog_data,
                TID,
                ADDRESS,
                NOSIX_ADDRESS_IPV4,
                MATCHED,
                SUMMARY,
                STATE,
                EVIDENCE,
                CAPTURE->wire_length
                        ? CAPTURE->wire_length
                        : CAPTURE->frame.length,
                PRINT_NEGATIVE
        );
        return ISTRUE;
}

static int kportscan_udp_icmpv6(
        _carry_forward * _prog_data,
        const unsigned char * TID,
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
        const uint8_t * INNER_UDP;
        uint16_t SOURCE_PORT;
        uint16_t DESTINATION_PORT;
        KPORTSCAN_PENDING * MATCHED;
        KPORTSCAN_STATE STATE;
        KPORTSCAN_EVIDENCE EVIDENCE;

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
                || INNER_IP[6] != NOSIX_IPPROTO_UDP
                || kportscan_address_matches(
                        INNER_IP + 24,
                        ADDRESS,
                        NOSIX_ADDRESS_IPV6
                ) != ISTRUE
        ) {
                return ISFALSE;
        }

        INNER_UDP = INNER_IP + 40;
        SOURCE_PORT = (uint16_t)(
                ((uint16_t)INNER_UDP[0] << 8)
                | INNER_UDP[1]
        );
        DESTINATION_PORT = (uint16_t)(
                ((uint16_t)INNER_UDP[2] << 8)
                | INNER_UDP[3]
        );

        MATCHED = kportscan_udp_find_pending(
                PENDING,
                PENDING_COUNT,
                DESTINATION_PORT,
                SOURCE_PORT
        );

        if (!MATCHED) {
                return ISFALSE;
        }

        STATE = ICMP6[1] == 4
                ? KPORTSCAN_STATE_CLOSED
                : KPORTSCAN_STATE_FILTERED;
        EVIDENCE = ICMP6[1] == 4
                ? KPORTSCAN_EVIDENCE_ICMP_PORT_UNREACHABLE
                : KPORTSCAN_EVIDENCE_ICMP_FILTERED;

        (void)kportscan_pcap_accept(
                _prog_data,
                TID,
                KPORTSCAN_PROTOCOL_UDP,
                NOSIX_ADDRESS_IPV6,
                MATCHED,
                CAPTURE
        );
        kportscan_udp_finalize(
                _prog_data,
                TID,
                ADDRESS,
                NOSIX_ADDRESS_IPV6,
                MATCHED,
                SUMMARY,
                STATE,
                EVIDENCE,
                CAPTURE->wire_length
                        ? CAPTURE->wire_length
                        : CAPTURE->frame.length,
                PRINT_NEGATIVE
        );
        return ISTRUE;
}

static int kportscan_udp_receive_batch(
        _carry_forward * _prog_data,
        const unsigned char * TID,
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

        memset(CATCH, 0x00, sizeof(CATCH));
        memset(&CAPTURE, 0x00, sizeof(CAPTURE));
        CAPTURE.frame.data = CATCH;
        CAPTURE.frame.capacity = sizeof(CATCH);

        for (;;) {
                const uint8_t * IP = NULL;
                size_t IP_LENGTH = 0;

                memset(CATCH, 0x00, sizeof(CATCH));
                CAPTURE.frame.length = 0;
                CAPTURE.wire_length = 0;
                CAPTURE.timestamp_ns = 0;
                CAPTURE.interface_index = 0;
                CAPTURE.flags = 0;

                STATUS = kwire_scan_read(_prog_data, &CAPTURE);

                if (STATUS == NOSIX_TIMEOUT) {
                        return NORMAL;
                }

                if (STATUS != NOSIX_OK && STATUS != NOSIX_TRUNCATED) {
                        kui_add_line_and_render(
                                NOTICE_ERROR
                                "UDP capture failed: " ANSI_COLOR_CYAN
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
                        kportscan_udp_direct_reply(
                                _prog_data,
                                TID,
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
                        (void)kportscan_udp_icmpv4(
                                _prog_data,
                                TID,
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
                        (void)kportscan_udp_icmpv6(
                                _prog_data,
                                TID,
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

int kportscan_udp(
        _carry_forward * _prog_data,
        const unsigned char * TID,
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
                || !ADDRESS
                || !PLAN
                || !SUMMARY
                || PLAN->UDP_COUNT == 0
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
                                        PLAN->UDP_PORTS,
                                        (uint16_t)NEXT_PORT
                                ) == ISTRUE
                        ) {
                                PENDING[COUNT].PORT = (uint16_t)NEXT_PORT;
                                PENDING[COUNT].SOURCE_PORT = kportscan_udp_source_port(
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
                                "UDP capture reset failed: " ANSI_COLOR_CYAN
                                "%s" ANSI_COLOR_RESET " (%d).",
                                kportscan_nosix_status_string(RESET_STATUS),
                                RESET_STATUS
                        );
                        RETURN_STATUS = ABNORMAL;
                        break;
                }

                for (uint32_t INDEX = 0; INDEX < COUNT; INDEX++) {
                        nosix_tx_packet_t PACKET;
                        uint8_t UPPER[KPORTSCAN_UDP_HEADER_LENGTH];
                        nosix_status_t STATUS;
                        KPCAP PORT_PCAP;

                        memset(&PORT_PCAP, 0x00, sizeof(PORT_PCAP));

                        if (
                                kportscan_udp_build(
                                        &PACKET,
                                        UPPER,
                                        sizeof(UPPER),
                                        ADDRESS,
                                        FAMILY,
                                        PENDING[INDEX].PORT,
                                        PENDING[INDEX].SOURCE_PORT
                                ) != NORMAL
                        ) {
                                kportscan_udp_finalize(
                                        _prog_data,
                                        TID,
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
                                        KPORTSCAN_PROTOCOL_UDP,
                                        FAMILY,
                                        &PENDING[INDEX],
                                        &PORT_PCAP
                                ) != NORMAL
                        ) {
                                kportscan_udp_finalize(
                                        _prog_data,
                                        TID,
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
                                kportscan_udp_finalize(
                                        _prog_data,
                                        TID,
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
                                                "UDP scan to " ANSI_COLOR_CYAN
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
                        kportscan_udp_receive_batch(
                                _prog_data,
                                TID,
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
                                kportscan_udp_finalize(
                                        _prog_data,
                                        TID,
                                        ADDRESS,
                                        FAMILY,
                                        &PENDING[INDEX],
                                        SUMMARY,
                                        KPORTSCAN_STATE_OPEN_FILTERED,
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
