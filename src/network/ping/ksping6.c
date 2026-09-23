// Copyright 2026 Jamison A. Drapeau
#include "ksping.h"
#include "kwire.h"
#include "kui.h"
#include "kscan.h"

#include <arpa/inet.h>
#include <nosix.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>

#define ICMPV6_ECHO_REQUEST 128U
#define ICMPV6_ECHO_REPLY   129U

// @@ Runtime ICMPv6 transaction sequence. Zero remains unused.
static uint16_t KSPING6_SEQUENCE = 0;

static const char * ksping6_nosix_status_string(nosix_status_t STATUS) {
        switch (STATUS) {
                case NOSIX_OK: return "NOSIX_OK";
                case NOSIX_TIMEOUT: return "NOSIX_TIMEOUT";
                case NOSIX_TRUNCATED: return "NOSIX_TRUNCATED";
                case NOSIX_ERR_ARGUMENT: return "NOSIX_ERR_ARGUMENT";
                case NOSIX_ERR_STATE: return "NOSIX_ERR_STATE";
                case NOSIX_ERR_MEMORY: return "NOSIX_ERR_MEMORY";
                case NOSIX_ERR_SYSTEM: return "NOSIX_ERR_SYSTEM";
                case NOSIX_ERR_ADDRESS: return "NOSIX_ERR_ADDRESS";
                case NOSIX_ERR_FRAME_TOO_LARGE: return "NOSIX_ERR_FRAME_TOO_LARGE";
                case NOSIX_ERR_UNSUPPORTED: return "NOSIX_ERR_UNSUPPORTED";
                case NOSIX_ERR_ROUTE: return "NOSIX_ERR_ROUTE";
                case NOSIX_ERR_NEIGHBOR: return "NOSIX_ERR_NEIGHBOR";
        }
        return "NOSIX_UNKNOWN";
}

static uint8_t ksping6_scan_result_from_nosix(nosix_status_t STATUS) {
        switch (STATUS) {
                case NOSIX_ERR_ROUTE:
                        return (uint8_t)(SCAN_RESULT_ERROR | SCAN_RESULT_ROUTE);
                case NOSIX_ERR_NEIGHBOR:
                        return (uint8_t)(SCAN_RESULT_ERROR | SCAN_RESULT_NEIGHBOR);
                default:
                        return SCAN_RESULT_ERROR;
        }
}

static double ksping6_elapsed_ms(
        const struct timespec * START,
        const struct timespec * END
) {
        double SECONDS;
        double NANOSECONDS;

        SECONDS = (double)(END->tv_sec - START->tv_sec) * 1000.0;
        NANOSECONDS = (double)(END->tv_nsec - START->tv_nsec) / 1000000.0;
        return SECONDS + NANOSECONDS;
}

static void ksping6_record_result(
        _carry_forward * _prog_data,
        uint8_t RESULT
) {
        if (kscan_record_icmpv6(_prog_data, RESULT) != NORMAL) {
                kui_add_line_and_render(
                        NOTICE_ERROR
                        "Failed to save ICMPv6 scan state."
                );
        }
}

static size_t ksping_build_icmpv6_echo_request(
        uint8_t * THROW,
        size_t THROW_MAX,
        uint16_t ECHO_ID,
        uint16_t ECHO_SEQ,
        const uint8_t * PAYLOAD,
        size_t PAYLOAD_LENGTH
) {
        const size_t HEADER_LENGTH = 8;
        size_t TOTAL_LENGTH = HEADER_LENGTH + PAYLOAD_LENGTH;

        if (!THROW || THROW_MAX < TOTAL_LENGTH) {
                return 0;
        }

        THROW[0] = (uint8_t)ICMPV6_ECHO_REQUEST;
        THROW[1] = 0x00;

        // @@ NOSIX owns the ICMPv6 checksum because it selects the IPv6 source.
        THROW[2] = 0x00;
        THROW[3] = 0x00;

        THROW[4] = (uint8_t)((ECHO_ID >> 8) & 0xffU);
        THROW[5] = (uint8_t)(ECHO_ID & 0xffU);
        THROW[6] = (uint8_t)((ECHO_SEQ >> 8) & 0xffU);
        THROW[7] = (uint8_t)(ECHO_SEQ & 0xffU);

        if (PAYLOAD_LENGTH > 0) {
                if (!PAYLOAD) {
                        return 0;
                }
                memcpy(THROW + HEADER_LENGTH, PAYLOAD, PAYLOAD_LENGTH);
        }

        return TOTAL_LENGTH;
}

static void ksping6_print_bytes(
        const uint8_t * DATA,
        size_t DATA_LENGTH
) {
        size_t OFFSET = 0;

        while (OFFSET < DATA_LENGTH) {
                char LINE[SUPSUP_BLOCK];
                size_t POSITION = 0;

                memset(LINE, 0x00, sizeof(LINE));

                POSITION += snprintf(
                        LINE + POSITION,
                        sizeof(LINE) - POSITION,
                        ANSI_COLOR_CYAN "%04zx  " ANSI_COLOR_RESET,
                        OFFSET
                );

                for (
                        size_t BYTE = 0;
                        BYTE < 16 && (OFFSET + BYTE) < DATA_LENGTH;
                        BYTE++
                ) {
                        POSITION += snprintf(
                                LINE + POSITION,
                                sizeof(LINE) - POSITION,
                                "%02X ",
                                DATA[OFFSET + BYTE]
                        );
                }

                kui_add_line_and_render("%s", LINE);
                OFFSET += 16;
        }
}

void ksping6(_carry_forward * _prog_data) {
        const char * TARGET_IP;
        uint8_t THROW[SUPSUP_BLOCK];
        size_t THROW_MAX;
        uint16_t ECHO_ID;
        uint16_t ECHO_SEQ;
        const uint8_t PAYLOAD[] =
                "Sent from Kaminowaku|sig=kaminowaku_icmpv6|";
        nosix_tx_packet_t PACKET;
        nosix_status_t STATUS;
        KPCAP PCAP;
        uint64_t SCAN_TIMESTAMP_NS;
        struct timespec TIME_START;
        struct timespec TIME_END;
        KWIRE_DEADLINE RX_DEADLINE;
        size_t TX_FRAME_LENGTH;
        uint8_t CATCH[OUT_BLOCK];
        nosix_capture_t CAPTURE;
        int8_t ECHO_REPLY_MATCHED;

        if (
                !_prog_data
                || !_prog_data->active_project_active_target
                || !_prog_data->active_project_active_target->PETAL
        ) {
                return;
        }

        TARGET_IP = (char*)_prog_data->active_project_active_target->PETAL->IPV6;

        if (!TARGET_IP || TARGET_IP[0] == 0x00) {
                kui_add_line_and_render(
                        NOTICE_NOPE
                        "Target does not have an IPv6 Address."
                );
                return;
        }

        if (_prog_data->nosix_net == NULL) {
                kui_add_line_and_render(
                        NOTICE_ERROR
                        "NOSIX network runtime is offline."
                );
                return;
        }

        if (_prog_data->gprof.tx_checksum_mode != CHECKSUM_AUTO) {
                kui_add_line_and_render(
                        NOTICE_ERROR
                        "ICMPv6 ping requires "
                        ANSI_COLOR_CYAN
                        "tx.checksum_mode=auto"
                        ANSI_COLOR_RESET
                        " so NOSIX can finalize the IPv6 pseudo-header checksum."
                );
                return;
        }

        if (_prog_data->debug_flag == ISTRUE) {
                kui_add_line_and_render(
                        "!["
                        ANSI_COLOR_MAGENTA
                        "ICMP6()"
                        ANSI_COLOR_RESET
                        "]> Save data to: "
                        ANSI_COLOR_CYAN
                        "%s"
                        ANSI_COLOR_RESET
                        , _prog_data->active_project_active_target_directory
                );
        }

        kui_add_line_and_render(
                NOTICE_INFO
                "Building ICMPv6 Packet..."
        );

        memset(THROW, 0x00, sizeof(THROW));
        THROW_MAX = 0;
        ECHO_ID = 0x6869;

        KSPING6_SEQUENCE++;
        if (KSPING6_SEQUENCE == 0) {
                KSPING6_SEQUENCE = 1;
        }

        ECHO_SEQ = KSPING6_SEQUENCE;

        THROW_MAX = ksping_build_icmpv6_echo_request(
                THROW,
                sizeof(THROW),
                ECHO_ID,
                ECHO_SEQ,
                PAYLOAD,
                sizeof(PAYLOAD) - 1
        );

        if (THROW_MAX == 0) {
                kui_add_line_and_render(
                        NOTICE_ERROR
                        "Failed to build ICMPv6 Packet"
                );
                return;
        }

        kui_add_line_and_render(
                NOTICE_INFO
                "Built ICMPv6 packet with "
                RGB_COLOR_BRIGHT_GREEN
                "%zu"
                ANSI_COLOR_RESET
                " bytes."
                , THROW_MAX
        );

        memset(&PACKET, 0x00, sizeof(PACKET));
        PACKET.destination.family = NOSIX_ADDRESS_IPV6;

        if (
                inet_pton(
                        AF_INET6,
                        TARGET_IP,
                        PACKET.destination.bytes.ipv6
                ) != 1
        ) {
                kui_add_line_and_render(
                        NOTICE_NOPE
                        "Invalid target IPv6 address."
                );
                return;
        }

        PACKET.ip_protocol = NOSIX_IPPROTO_ICMPV6;
        PACKET.upper_layer = THROW;
        PACKET.upper_layer_length = THROW_MAX;
        PACKET.flags = 0;

        memset(&PCAP, 0x00, sizeof(PCAP));
        SCAN_TIMESTAMP_NS = kscan_now_ns();

        if (
                kwire_pcap_open(
                        _prog_data,
                        &PCAP,
                        "ICMPv6",
                        _prog_data->active_project_active_target->TID,
                        SCAN_TIMESTAMP_NS
                ) != NORMAL
        ) {
                kui_add_line_and_render(
                        NOTICE_ERROR
                        "Failed to open ICMPv6 packet capture."
                );
                return;
        }

        if (_prog_data->debug_flag == ISTRUE) {
                kui_add_line_and_render(
                        "!["
                        ANSI_COLOR_MAGENTA
                        "ICMP6()"
                        ANSI_COLOR_RESET
                        "]> PCAP: "
                        ANSI_COLOR_CYAN
                        "%s"
                        ANSI_COLOR_RESET
                        , PCAP.PATH
                );
        }

        // @@ IO rule: print TX bytes immediately before the transmit path.
        ksping6_print_bytes(THROW, THROW_MAX);

        memset(&TIME_START, 0x00, sizeof(TIME_START));
        memset(&TIME_END, 0x00, sizeof(TIME_END));
        clock_gettime(CLOCK_MONOTONIC, &TIME_START);

        TX_FRAME_LENGTH = 0;

        STATUS = kwire_write(
                _prog_data,
                &PCAP,
                &PACKET,
                &TX_FRAME_LENGTH
        );

        if (STATUS != NOSIX_OK) {
                ksping6_record_result(
                        _prog_data,
                        ksping6_scan_result_from_nosix(STATUS)
                );

                kwire_pcap_close(&PCAP);

                kui_add_line_and_render(
                        NOTICE_ERROR
                        "ICMPv6 transmission failed: "
                        ANSI_COLOR_CYAN
                        "%s"
                        ANSI_COLOR_RESET
                        " (%d)%s"
                        , ksping6_nosix_status_string(STATUS)
                        , STATUS
                        , STATUS == NOSIX_ERR_ROUTE
                                ? " - no usable IPv6 route."
                                : STATUS == NOSIX_ERR_SYSTEM
                                        ? " - check host IPv6 routing/platform state."
                                        : ""
                );
                return;
        }

        kui_add_line_and_render(
                NOTICE_TRANSMISSION
                "ICMPv6 Echo Request sent to "
                ANSI_COLOR_CYAN
                "%s"
                ANSI_COLOR_RESET
                " ("
                RGB_COLOR_BRIGHT_GREEN
                "%zu"
                ANSI_COLOR_RESET
                " wire bytes)."
                , TARGET_IP
                , TX_FRAME_LENGTH
        );

        memset(CATCH, 0x00, sizeof(CATCH));
        memset(&CAPTURE, 0x00, sizeof(CAPTURE));

        CAPTURE.frame.data = CATCH;
        CAPTURE.frame.capacity = sizeof(CATCH);
        ECHO_REPLY_MATCHED = ISFALSE;
        if (kwire_deadline_start(&RX_DEADLINE, _prog_data->gprof.rx_timeout_ms) != NORMAL) {
                ksping6_record_result(_prog_data, SCAN_RESULT_ERROR);
                kwire_pcap_close(&PCAP);
                return;
        }

        for (;;) {
                uint8_t * FRAME;
                size_t L2_HEADER_LENGTH;
                uint16_t ETHERTYPE;
                uint8_t * IPV6;
                uint8_t * ICMP6;
                uint16_t REPLY_ID;
                uint16_t REPLY_SEQ;
                uint16_t IPV6_PAYLOAD_LENGTH;
                size_t ICMP_REPLY_LENGTH;
                size_t ICMP_CAPTURE_LENGTH;
                uint8_t REPLY_HOP_LIMIT;
                char RESPONSE_IP[INET6_ADDRSTRLEN];
                double RTT_MS;

                memset(CATCH, 0x00, sizeof(CATCH));
                CAPTURE.frame.length = 0;
                CAPTURE.wire_length = 0;
                CAPTURE.timestamp_ns = 0;
                CAPTURE.interface_index = 0;
                CAPTURE.flags = 0;

                STATUS = kwire_read_until(
                        _prog_data, &PCAP, &CAPTURE, &RX_DEADLINE
                );

                if (STATUS == NOSIX_TIMEOUT) {
                        break;
                }

                if (
                        STATUS != NOSIX_OK
                        && STATUS != NOSIX_TRUNCATED
                ) {
                        ksping6_record_result(
                                _prog_data,
                                SCAN_RESULT_ERROR
                        );
                        kwire_pcap_close(&PCAP);

                        kui_add_line_and_render(
                                NOTICE_ERROR
                                "ICMPv6 capture failed: "
                                ANSI_COLOR_CYAN
                                "%s"
                                ANSI_COLOR_RESET
                                " (%d)"
                                , ksping6_nosix_status_string(STATUS)
                                , STATUS
                        );
                        return;
                }

                FRAME = CAPTURE.frame.data;
                L2_HEADER_LENGTH = 14;

                // Minimum Ethernet + IPv6 + ICMPv6 Echo header.
                if (CAPTURE.frame.length < 62) {
                        continue;
                }

                ETHERTYPE = (uint16_t)(
                        ((uint16_t)FRAME[12] << 8)
                        | FRAME[13]
                );

                // @@ Single VLAN tag support.
                if (
                        ETHERTYPE == 0x8100
                        || ETHERTYPE == 0x88A8
                ) {
                        if (CAPTURE.frame.length < 66) {
                                continue;
                        }

                        ETHERTYPE = (uint16_t)(
                                ((uint16_t)FRAME[16] << 8)
                                | FRAME[17]
                        );
                        L2_HEADER_LENGTH = 18;
                }

                if (ETHERTYPE != 0x86dd) {
                        continue;
                }

                IPV6 = FRAME + L2_HEADER_LENGTH;

                if ((IPV6[0] >> 4) != 6) {
                        continue;
                }

                if (
                        CAPTURE.frame.length
                        < L2_HEADER_LENGTH + 40 + 8
                ) {
                        continue;
                }

                // @@ First pass: direct ICMPv6 replies without extension headers.
                if (IPV6[6] != NOSIX_IPPROTO_ICMPV6) {
                        continue;
                }

                // Reply must originate from the target.
                if (
                        memcmp(
                                IPV6 + 8,
                                PACKET.destination.bytes.ipv6,
                                16
                        ) != MATCH
                ) {
                        continue;
                }

                ICMP6 = IPV6 + 40;

                if (
                        ICMP6[0] != ICMPV6_ECHO_REPLY
                        || ICMP6[1] != 0x00
                ) {
                        continue;
                }

                REPLY_ID = (uint16_t)(
                        ((uint16_t)ICMP6[4] << 8)
                        | ICMP6[5]
                );

                REPLY_SEQ = (uint16_t)(
                        ((uint16_t)ICMP6[6] << 8)
                        | ICMP6[7]
                );

                if (
                        REPLY_ID != ECHO_ID
                        || REPLY_SEQ != ECHO_SEQ
                ) {
                        continue;
                }

                // @@ We have our packet.
                kwire_rx_accept(
                        _prog_data,
                        &CAPTURE
                );

                clock_gettime(CLOCK_MONOTONIC, &TIME_END);
                RTT_MS = ksping6_elapsed_ms(&TIME_START, &TIME_END);

                memset(RESPONSE_IP, 0x00, sizeof(RESPONSE_IP));

                if (
                        inet_ntop(
                                AF_INET6,
                                IPV6 + 8,
                                RESPONSE_IP,
                                sizeof(RESPONSE_IP)
                        ) == NULL
                ) {
                        snprintf(
                                RESPONSE_IP,
                                sizeof(RESPONSE_IP),
                                "%s",
                                TARGET_IP
                        );
                }

                IPV6_PAYLOAD_LENGTH = (uint16_t)(
                        ((uint16_t)IPV6[4] << 8)
                        | IPV6[5]
                );

                ICMP_CAPTURE_LENGTH = CAPTURE.frame.length - (
                        L2_HEADER_LENGTH + 40
                );

                ICMP_REPLY_LENGTH = IPV6_PAYLOAD_LENGTH;
                if (ICMP_REPLY_LENGTH > ICMP_CAPTURE_LENGTH) {
                        ICMP_REPLY_LENGTH = ICMP_CAPTURE_LENGTH;
                }

                REPLY_HOP_LIMIT = IPV6[7];

                // @@ IO rule: RX notice comes before RX bytes.
                kui_add_line_and_render(
                        NOTICE_RECIEVE
                        "Received packet with "
                        RGB_COLOR_BRIGHT_GREEN
                        "%zu"
                        ANSI_COLOR_RESET
                        " bytes."
                        , ICMP_REPLY_LENGTH
                );

                ksping6_print_bytes(ICMP6, ICMP_REPLY_LENGTH);

                kui_add_line_and_render(
                        NOTICE_INFO
                        "ICMPv6 Source = "
                        ANSI_COLOR_CYAN
                        "%s"
                        ANSI_COLOR_RESET
                        , RESPONSE_IP
                );

                kui_add_line_and_render(
                        NOTICE_INFO
                        "ICMPv6 Type = Echo Reply ("
                        ANSI_COLOR_CYAN
                        "%u"
                        ANSI_COLOR_RESET
                        ")"
                        , ICMP6[0]
                );

                kui_add_line_and_render(
                        NOTICE_INFO
                        "ICMPv6 Code = "
                        ANSI_COLOR_CYAN
                        "%u"
                        ANSI_COLOR_RESET
                        , ICMP6[1]
                );

                kui_add_line_and_render(
                        NOTICE_INFO
                        "ICMPv6 Identifier = "
                        ANSI_COLOR_CYAN
                        "0x%04X"
                        ANSI_COLOR_RESET
                        , REPLY_ID
                );

                kui_add_line_and_render(
                        NOTICE_INFO
                        "ICMPv6 Sequence = "
                        ANSI_COLOR_CYAN
                        "%u"
                        ANSI_COLOR_RESET
                        , REPLY_SEQ
                );

                kui_add_line_and_render(
                        NOTICE_INFO
                        "ICMPv6 Hop Limit = "
                        ANSI_COLOR_CYAN
                        "%u"
                        ANSI_COLOR_RESET
                        , REPLY_HOP_LIMIT
                );

                kui_add_line_and_render(
                        NOTICE_INFO
                        "ICMPv6 Time = "
                        ANSI_COLOR_CYAN
                        "%.3f ms"
                        ANSI_COLOR_RESET
                        , RTT_MS
                );

                ECHO_REPLY_MATCHED = ISTRUE;
                break;
        }

        if (ECHO_REPLY_MATCHED == ISFALSE) {
                kui_add_line_and_render(
                        NOTICE_NOPE
                        "Request timed out after "
                        ANSI_COLOR_CYAN
                        "%d"
                        ANSI_COLOR_RESET
                        " ms."
                        , _prog_data->gprof.rx_timeout_ms
                );

                ksping6_record_result(
                        _prog_data,
                        SCAN_RESULT_TIMEOUT
                );
        } else {
                ksping6_record_result(
                        _prog_data,
                        SCAN_RESULT_REPLY
                );
        }

        kwire_pcap_close(&PCAP);
}
