// Copyright 2026 Jamison A. Drapeau
#include "ksping.h"
#include "kwire.h"
#include "helpers.h"
#include "kui.h"
#include "tlib.h"
#include <nosix.h>
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <netinet/in.h>

// @@ ICMPv4 Section // RFC 792
/*
------------------------
Summary of Message Types
------------------------
 0  Echo Reply
 3  Destination Unreachable
 4  Source Quench
 5  Redirect
 8  Echo
11  Time Exceeded
12  Parameter Problem
13  Timestamp
14  Timestamp Reply
15  Information Request
16  Information Reply
------------------------
*/
// [ type | code | cksum_hi | cksum_lo | id_hi | id_lo | seq_hi | seq_lo | payload... ]

#define ICMPV4_ECHO_REQUEST  8
#define ICMPV4_ECHO_REPLY    0

typedef enum status_codes_ICMP4 {
        KSPING4_OK = 0,
        KSPING4_TIMEOUT,
        KSPING4_BAD_IPV4,
        KSPING4_SOCKET_FAIL,
        KSPING4_SEND_FAIL,
        KSPING4_RECV_FAIL,
        KSPING4_NOT_ECHOREPLY
} KSPING4_STATUS;

// @@ Runtime transaction sequence. Zero remains unused.
static uint16_t KSPING4_SEQUENCE = 0;

// Ones compliment checksum
static uint16_t ksping4_checksum_16 (const uint8_t *bb, size_t l) {
        
        // Sum and size
        uint32_t sum = 0;
        size_t     i = 0;

        // Byte iterator and bitwise operand
        while (i + 1 < l) {
                sum += (uint16_t)(bb[i] << 8) | bb[i + 1];
                i += 2;
        }

        // Odd byte handling
        if (i < l) {
                sum += (uint16_t)(bb[i] << 8);
        }

        // Fold carry bits back into the lower 16 bits
        while (sum >> 16) {
                sum = (sum & 0xFFFFu) + (sum >> 16);
        }

        // Return checksum
        return (uint16_t)(~sum);

}

static size_t ksping_build_icmpv4_echo_request (
        uint8_t *THROW,
        size_t THROW_MAX,
        uint16_t ECHO_ID,
        uint16_t ECHO_SEQ,
        const uint8_t *PAYLOAD,
        size_t PAYLOAD_LENGTH
) {
        size_t HEADER_LENGTH = 8; // RFC 792 Specifies 8 byte header
        size_t TOTAL_LENGTH = HEADER_LENGTH + PAYLOAD_LENGTH;
        if (!THROW || THROW_MAX < TOTAL_LENGTH) {
                return 0;
        }
        // Type/Code
        THROW[0] = (uint8_t)ICMPV4_ECHO_REQUEST;
        THROW[1] = 0x00;
        // Checksum, init to zero per RFC 792
        THROW[2] = 0x00;
        THROW[3] = 0x00;
        // Network Byte Order
        THROW[4] = (uint8_t)((ECHO_ID >> 8) & 0xFF);
        THROW[5] = (uint8_t)(ECHO_ID & 0xFF);
        // Sequence Order
        THROW[6] = (uint8_t)((ECHO_SEQ >> 8) & 0xFF);
        THROW[7] = (uint8_t)(ECHO_SEQ & 0xFF);
        // Payload
        if (PAYLOAD_LENGTH > 0) {
                if (!PAYLOAD) {
                        return 0;
                } else {
                        memcpy(THROW + HEADER_LENGTH, PAYLOAD, PAYLOAD_LENGTH);
                }
        }
        // Checksum
        uint16_t CHECKSUM = ksping4_checksum_16(THROW, TOTAL_LENGTH);
        THROW[2] = (uint8_t)((CHECKSUM >> 8) & 0xFF);
        THROW[3] = (uint8_t)(CHECKSUM & 0xFF);
        return TOTAL_LENGTH;
}

// @@ Human readable NOSIX errors
static const char * ksping4_nosix_status_string (nosix_status_t STATUS) {
        switch (STATUS) {
                case NOSIX_OK:
                        return "NOSIX_OK";
                case NOSIX_TIMEOUT:
                        return "NOSIX_TIMEOUT";
                case NOSIX_TRUNCATED:
                        return "NOSIX_TRUNCATED";
                case NOSIX_ERR_ARGUMENT:
                        return "NOSIX_ERR_ARGUMENT";
                case NOSIX_ERR_STATE:
                        return "NOSIX_ERR_STATE";
                case NOSIX_ERR_MEMORY:
                        return "NOSIX_ERR_MEMORY";
                case NOSIX_ERR_SYSTEM:
                        return "NOSIX_ERR_SYSTEM";
                case NOSIX_ERR_ADDRESS:
                        return "NOSIX_ERR_ADDRESS";
                case NOSIX_ERR_FRAME_TOO_LARGE:
                        return "NOSIX_ERR_FRAME_TOO_LARGE";
                case NOSIX_ERR_UNSUPPORTED:
                        return "NOSIX_ERR_UNSUPPORTED";
                case NOSIX_ERR_ROUTE:
                        return "NOSIX_ERR_ROUTE";
                case NOSIX_ERR_NEIGHBOR:
                        return "NOSIX_ERR_NEIGHBOR";
        }
        return "NOSIX_UNKNOWN";
}

// @@ Preserve specific TX failure class while keeping legacy ERROR compatibility
static uint8_t ksping4_scan_result_from_nosix (nosix_status_t STATUS) {
        switch (STATUS) {
                case NOSIX_ERR_ROUTE:
                        return (uint8_t)(SCAN_RESULT_ERROR | SCAN_RESULT_ROUTE);
                case NOSIX_ERR_NEIGHBOR:
                        return (uint8_t)(SCAN_RESULT_ERROR | SCAN_RESULT_NEIGHBOR);
                default:
                        return SCAN_RESULT_ERROR;
        }
}

// @@ Monotonic transaction timer
static double ksping4_elapsed_ms (
        const struct timespec * START,
        const struct timespec * END
) {
        double SECONDS;
        double NANOSECONDS;

        SECONDS = (double)(END->tv_sec - START->tv_sec) * 1000.0;
        NANOSECONDS = (double)(END->tv_nsec - START->tv_nsec) / 1000000.0;

        return SECONDS + NANOSECONDS;
}

// @@ Persist ICMPv4 scan state without duplicating target write logic
static void ksping4_record_result(
        _carry_forward * _prog_data,
        uint8_t RESULT
) {
        if (
                kscan_record_icmpv4(
                        _prog_data,
                        RESULT
                ) != NORMAL
        ) {
                kui_add_line_and_render(
                        NOTICE_ERROR
                        "Failed to save ICMPv4 scan state."
                );
        }
}

// ICMP 4 Transaction
static void ICMP4 ( _carry_forward * _prog_data ) {
        const char * TARGET_IP = (char*)_prog_data->active_project_active_target->PETAL->IPV4;
        if (!TARGET_IP || TARGET_IP[0] == 0x00) {
                kui_add_line_and_render(
                        NOTICE_NOPE
                        "Target does not have an IPv4 Address."
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

        if (_prog_data->debug_flag == ISTRUE) {
                kui_add_line_and_render(
                        "!["
                        ANSI_COLOR_MAGENTA
                        "ICMP4()"
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
                "Building ICMPv4 Packet..."
        );

        // @@ Build upper-layer ICMP data
        uint8_t THROW[SUPSUP_BLOCK]; memset(THROW, 0x00, SUPSUP_BLOCK);
        size_t  THROW_MAX = 0;
        uint16_t ECHO_ID = 0x6869;

        KSPING4_SEQUENCE++;
        if (KSPING4_SEQUENCE == 0) {
                KSPING4_SEQUENCE = 1;
        }

        uint16_t ECHO_SEQ = KSPING4_SEQUENCE;
        const uint8_t PAYLOAD[] = "Sent from Kaminowaku|sig=kaminowaku_icmpv4|";

        THROW_MAX = ksping_build_icmpv4_echo_request(
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
                        "Failed to build ICMPv4 Packet"
                );
                return;
        }

        kui_add_line_and_render(
                NOTICE_INFO
                "Built ICMPv4 packet with "
                RGB_COLOR_BRIGHT_GREEN
                "%zu"
                ANSI_COLOR_RESET
                " bytes."
                , THROW_MAX
        );

        // Do wire things
        ///////////////////////////////////////////////////////////////////////////////////////////////////////////////

        nosix_status_t STATUS;

        // @@ Build NOSIX managed packet descriptor
        nosix_tx_packet_t PACKET;
        memset(&PACKET, 0x00, sizeof(PACKET));

        PACKET.destination.family = NOSIX_ADDRESS_IPV4;

        if (
                inet_pton(
                        AF_INET,
                        TARGET_IP,
                        PACKET.destination.bytes.ipv4
                ) != 1
        ) {
                kui_add_line_and_render(
                        NOTICE_NOPE
                        "Invalid target IPv4 address."
                );
                return;
        }

        PACKET.ip_protocol = NOSIX_IPPROTO_ICMP;
        PACKET.upper_layer = THROW;
        PACKET.upper_layer_length = THROW_MAX;
        PACKET.flags = 0;

        // @@ Open one PCAP for this target scan
        KPCAP PCAP;
        uint64_t SCAN_TIMESTAMP_NS = kscan_now_ns();
        memset(&PCAP, 0x00, sizeof(PCAP));

        if (
                kwire_pcap_open(
                        _prog_data,
                        &PCAP,
                        "ICMPv4",
                        _prog_data->active_project_active_target->TID,
                        SCAN_TIMESTAMP_NS
                ) != NORMAL
        ) {
                kui_add_line_and_render(
                        NOTICE_ERROR
                        "Failed to open ICMPv4 packet capture."
                );
                return;
        }

        if (_prog_data->debug_flag == ISTRUE) {
                kui_add_line_and_render(
                        "!["
                        ANSI_COLOR_MAGENTA
                        "ICMP4()"
                        ANSI_COLOR_RESET
                        "]> PCAP: "
                        ANSI_COLOR_CYAN
                        "%s"
                        ANSI_COLOR_RESET
                        , PCAP.PATH
                );
        }

        // @@ IO rule: print TX bytes immediately before the transmit path.
        size_t i = 0;
        while (i < THROW_MAX) {
                char line[SUPSUP_BLOCK];
                size_t pos = 0;
                pos += snprintf(
                        line + pos,
                        sizeof(line) - pos,
                        ANSI_COLOR_CYAN "%04zx  " ANSI_COLOR_RESET,
                        i
                );
                for (size_t j = 0; j < 16 && (i + j) < THROW_MAX; j++) {
                        pos += snprintf(
                                line + pos,
                                sizeof(line) - pos,
                                "%02X ",
                                THROW[i + j]
                        );
                }
                kui_add_line_and_render("%s", line);
                i += 16;
        }

        // @@ Start transaction timer immediately before transmission
        struct timespec TIME_START;
        struct timespec TIME_END;
        memset(&TIME_START, 0x00, sizeof(TIME_START));
        memset(&TIME_END, 0x00, sizeof(TIME_END));

        clock_gettime(CLOCK_MONOTONIC, &TIME_START);

        // @@ KWIRE writes through NOSIX and records the exact transmitted surface
        size_t TX_FRAME_LENGTH = 0;

        STATUS = kwire_write(
                _prog_data,
                &PCAP,
                &PACKET,
                &TX_FRAME_LENGTH
        );

        if (STATUS != NOSIX_OK) {
                ksping4_record_result(
                        _prog_data,
                        ksping4_scan_result_from_nosix(STATUS)
                );
                kwire_pcap_close(&PCAP);
                kui_add_line_and_render(
                        NOTICE_ERROR
                        "ICMPv4 transmission failed: "
                        ANSI_COLOR_CYAN
                        "%s"
                        ANSI_COLOR_RESET
                        " (%d)"
                        , ksping4_nosix_status_string(STATUS)
                        , STATUS
                );
                return;
        }

        nosix_tx_surface_t TX_SURFACE = nosix_last_tx_surface(
                _prog_data->nosix_net
        );

        kui_add_line_and_render(
                NOTICE_TRANSMISSION
                "ICMPv4 Echo Request sent to "
                ANSI_COLOR_CYAN
                "%s"
                ANSI_COLOR_RESET
                " ("
                RGB_COLOR_BRIGHT_GREEN
                "%zu"
                ANSI_COLOR_RESET
                "%s)."
                , TARGET_IP
                , TX_FRAME_LENGTH
                , TX_SURFACE == NOSIX_TX_SURFACE_IPV4_LOCAL
                        ? " IP bytes"
                        : " wire bytes"
        );

        // @@ Caller-owned NOSIX capture storage
        uint8_t CATCH[OUT_BLOCK];
        memset(CATCH, 0x00, sizeof(CATCH));

        nosix_capture_t CAPTURE;
        memset(&CAPTURE, 0x00, sizeof(CAPTURE));

        CAPTURE.frame.data = CATCH;
        CAPTURE.frame.capacity = sizeof(CATCH);

        int8_t ECHO_REPLY_MATCHED = ISFALSE;

        // @@ Transaction receive loop
        for (;;) {

                // Clear caller data between captured frames
                memset(CATCH, 0x00, sizeof(CATCH));
                CAPTURE.frame.length = 0;
                CAPTURE.wire_length = 0;
                CAPTURE.timestamp_ns = 0;
                CAPTURE.interface_index = 0;
                CAPTURE.flags = 0;

                STATUS = kwire_read(
                        _prog_data,
                        &PCAP,
                        &CAPTURE
                );

                // @@ NOSIX timeout
                if (STATUS == NOSIX_TIMEOUT) {
                        break;
                }

                // @@ Positive truncated return still contains usable capture data
                if (
                        STATUS != NOSIX_OK
                        &&
                        STATUS != NOSIX_TRUNCATED
                ) {
                        ksping4_record_result(
                                _prog_data,
                                SCAN_RESULT_ERROR
                        );
                        kwire_pcap_close(&PCAP);
                        kui_add_line_and_render(
                                NOTICE_ERROR
                                "ICMPv4 capture failed: "
                                ANSI_COLOR_CYAN
                                "%s"
                                ANSI_COLOR_RESET
                                " (%d)"
                                , ksping4_nosix_status_string(STATUS)
                                , STATUS
                        );
                        return;
                }

                uint8_t * FRAME = CAPTURE.frame.data;
                size_t L2_HEADER_LENGTH = 0;

                if (CAPTURE.flags & NOSIX_CAPTURE_IPV4_LOCAL) {
                        if (CAPTURE.frame.length < 28) {
                                continue;
                        }
                } else {
                        // Minimum Ethernet + IPv4 + ICMP Echo header
                        if (CAPTURE.frame.length < 42) {
                                continue;
                        }

                        L2_HEADER_LENGTH = 14;

                        // @@ Ethernet EtherType
                        uint16_t ETHERTYPE = (uint16_t)(
                                ((uint16_t)FRAME[12] << 8)
                                |
                                FRAME[13]
                        );

                        // @@ Single VLAN tag support
                        if (
                                ETHERTYPE == 0x8100
                                ||
                                ETHERTYPE == 0x88A8
                        ) {
                                if (CAPTURE.frame.length < 46) {
                                        continue;
                                }

                                ETHERTYPE = (uint16_t)(
                                        ((uint16_t)FRAME[16] << 8)
                                        |
                                        FRAME[17]
                                );

                                L2_HEADER_LENGTH = 18;
                        }

                        if (ETHERTYPE != 0x0800) {
                                continue;
                        }
                }

                uint8_t * IPV4 = FRAME + L2_HEADER_LENGTH;

                // IPv4 version
                if ((IPV4[0] >> 4) != 4) {
                        continue;
                }

                // IPv4 header length
                size_t IPV4_HEADER_LENGTH = (size_t)(IPV4[0] & 0x0F) * 4;

                if (IPV4_HEADER_LENGTH < 20) {
                        continue;
                }

                if (
                        CAPTURE.frame.length
                        <
                        L2_HEADER_LENGTH + IPV4_HEADER_LENGTH + 8
                ) {
                        continue;
                }

                // ICMP protocol
                if (IPV4[9] != NOSIX_IPPROTO_ICMP) {
                        continue;
                }

                // Reply must originate from the target
                if (
                        memcmp(
                                IPV4 + 12,
                                PACKET.destination.bytes.ipv4,
                                4
                        ) != MATCH
                ) {
                        continue;
                }

                uint8_t * ICMP = IPV4 + IPV4_HEADER_LENGTH;

                // ICMP Echo Reply
                if (
                        ICMP[0] != ICMPV4_ECHO_REPLY
                        ||
                        ICMP[1] != 0x00
                ) {
                        continue;
                }

                uint16_t REPLY_ID = (uint16_t)(
                        ((uint16_t)ICMP[4] << 8)
                        |
                        ICMP[5]
                );

                uint16_t REPLY_SEQ = (uint16_t)(
                        ((uint16_t)ICMP[6] << 8)
                        |
                        ICMP[7]
                );

                // Match this response to this transaction
                if (
                        REPLY_ID != ECHO_ID
                        ||
                        REPLY_SEQ != ECHO_SEQ
                ) {
                        continue;
                }

                // @@ We have our packet
                kwire_rx_accept(
                        _prog_data,
                        &CAPTURE
                );

                clock_gettime(CLOCK_MONOTONIC, &TIME_END);

                double RTT_MS = ksping4_elapsed_ms(
                        &TIME_START,
                        &TIME_END
                );

                char RESPONSE_IP[INET_ADDRSTRLEN];
                memset(RESPONSE_IP, 0x00, sizeof(RESPONSE_IP));

                if (
                        inet_ntop(
                                AF_INET,
                                IPV4 + 12,
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

                uint16_t IPV4_TOTAL_LENGTH = (uint16_t)(
                        ((uint16_t)IPV4[2] << 8)
                        |
                        IPV4[3]
                );

                size_t ICMP_REPLY_LENGTH = 0;
                size_t ICMP_CAPTURE_LENGTH = CAPTURE.frame.length - (
                        L2_HEADER_LENGTH + IPV4_HEADER_LENGTH
                );

                if (IPV4_TOTAL_LENGTH >= IPV4_HEADER_LENGTH) {
                        ICMP_REPLY_LENGTH = IPV4_TOTAL_LENGTH - IPV4_HEADER_LENGTH;
                }

                if (ICMP_REPLY_LENGTH > ICMP_CAPTURE_LENGTH) {
                        ICMP_REPLY_LENGTH = ICMP_CAPTURE_LENGTH;
                }

                uint8_t REPLY_TTL = IPV4[8];

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

                // @@ Print returned ICMP upper-layer data
                size_t RETURN_OFFSET = 0;
                while (RETURN_OFFSET < ICMP_REPLY_LENGTH) {
                        char line[SUPSUP_BLOCK];
                        size_t pos = 0;

                        pos += snprintf(
                                line + pos,
                                sizeof(line) - pos,
                                ANSI_COLOR_CYAN "%04zx  " ANSI_COLOR_RESET,
                                RETURN_OFFSET
                        );

                        for (
                                size_t RETURN_BYTE = 0;
                                RETURN_BYTE < 16 && (RETURN_OFFSET + RETURN_BYTE) < ICMP_REPLY_LENGTH;
                                RETURN_BYTE++
                        ) {
                                pos += snprintf(
                                        line + pos,
                                        sizeof(line) - pos,
                                        "%02X ",
                                        ICMP[RETURN_OFFSET + RETURN_BYTE]
                                );
                        }

                        kui_add_line_and_render("%s", line);
                        RETURN_OFFSET += 16;
                }

                kui_add_line_and_render(
                        NOTICE_INFO
                        "ICMPv4 Source = "
                        ANSI_COLOR_CYAN
                        "%s"
                        ANSI_COLOR_RESET
                        , RESPONSE_IP
                );

                kui_add_line_and_render(
                        NOTICE_INFO
                        "ICMPv4 Type = Echo Reply ("
                        ANSI_COLOR_CYAN
                        "%u"
                        ANSI_COLOR_RESET
                        ")"
                        , ICMP[0]
                );

                kui_add_line_and_render(
                        NOTICE_INFO
                        "ICMPv4 Code = "
                        ANSI_COLOR_CYAN
                        "%u"
                        ANSI_COLOR_RESET
                        , ICMP[1]
                );

                kui_add_line_and_render(
                        NOTICE_INFO
                        "ICMPv4 Identifier = "
                        ANSI_COLOR_CYAN
                        "0x%04X"
                        ANSI_COLOR_RESET
                        , REPLY_ID
                );

                kui_add_line_and_render(
                        NOTICE_INFO
                        "ICMPv4 Sequence = "
                        ANSI_COLOR_CYAN
                        "%u"
                        ANSI_COLOR_RESET
                        , REPLY_SEQ
                );

                kui_add_line_and_render(
                        NOTICE_INFO
                        "ICMPv4 TTL = "
                        ANSI_COLOR_CYAN
                        "%u"
                        ANSI_COLOR_RESET
                        , REPLY_TTL
                );

                kui_add_line_and_render(
                        NOTICE_INFO
                        "ICMPv4 Time = "
                        ANSI_COLOR_CYAN
                        "%.3f ms"
                        ANSI_COLOR_RESET
                        , RTT_MS
                );

                ECHO_REPLY_MATCHED = ISTRUE;
                break;
        }

        // @@ No response
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

                ksping4_record_result(
                        _prog_data,
                        SCAN_RESULT_TIMEOUT
                );
        } else {
                ksping4_record_result(
                        _prog_data,
                        SCAN_RESULT_REPLY
                );
        }

        kwire_pcap_close(&PCAP);
        return;
}



// Middle layer wrapper
void ksping_single(_carry_forward * _prog_data) {
        if (
                _prog_data->cmd_tokens_count > 1
                &&
                strlen((char*)_prog_data->cmd_tokens[1]) == 2
        ) {
                switch (_prog_data->cmd_tokens[1][1]) {
                        case 0x34: { // '4'
                                if (_prog_data->debug_flag == ISTRUE) {
                                        kui_add_line_and_render(
                                                "!["
                                                ANSI_COLOR_MAGENTA
                                                "ksping_single()"
                                                ANSI_COLOR_RESET
                                                "]> ICMPv4 Initializing."
                                        );
                                }
                                ICMP4(_prog_data);
                                break;
                        }
                        case 0x36: { // '6'
                                if (_prog_data->debug_flag == ISTRUE) {
                                        kui_add_line_and_render(
                                                "!["
                                                ANSI_COLOR_MAGENTA
                                                "ksping_single()"
                                                ANSI_COLOR_RESET
                                                "]> ICMPv6 Initializing."
                                        );
                                }
                                ksping6(_prog_data);
                                break;
                        }
                        default: {
                                kui_add_line("< Usage: ping [ -4 | -6 ]");
                                break;
                        }
                }
        } else {
                kui_add_line("< Usage: ping [ -4 | -6 ]");
        }
}

// @@ Project-context ping wrapper
void ksping_multi(_carry_forward * _prog_data) {
        FLOWER * CURRENT_FLOWER_NODE_INDEX = NULL;
        FLOWER * PREVIOUS_ACTIVE_TARGET = NULL;
        char * PREVIOUS_ACTIVE_TARGET_DIRECTORY = NULL;

        if (
                !_prog_data
                ||
                !_prog_data->active_project_flower
        ) {
                return;
        }

        PREVIOUS_ACTIVE_TARGET = _prog_data->active_project_active_target;
        PREVIOUS_ACTIVE_TARGET_DIRECTORY = _prog_data->active_project_active_target_directory;

        CURRENT_FLOWER_NODE_INDEX = _prog_data->active_project_flower->NEXT;

        while (CURRENT_FLOWER_NODE_INDEX) {
                TARGET PETAL;
                TARGET * PREVIOUS_PETAL = CURRENT_FLOWER_NODE_INDEX->PETAL;
                char PETAL_PATH[MAX_PATH];
                char TARGET_DIRECTORY[MAX_PATH];

                kui_progress_advance();

                memset(&PETAL, 0x00, sizeof(PETAL));
                memset(PETAL_PATH, 0x00, sizeof(PETAL_PATH));
                memset(TARGET_DIRECTORY, 0x00, sizeof(TARGET_DIRECTORY));

                if (
                        build_petal_path(
                                PETAL_PATH,
                                sizeof(PETAL_PATH),
                                _prog_data->wd,
                                _prog_data->active_project,
                                CURRENT_FLOWER_NODE_INDEX->TID
                        ) != NORMAL
                ) {
                        kui_add_line_and_render(
                                NOTICE_ERROR
                                "Failed to build target path for "
                                RGB_COLOR_RICH_RED
                                "%s"
                                ANSI_COLOR_RESET
                                "."
                                , CURRENT_FLOWER_NODE_INDEX->TID
                        );
                        CURRENT_FLOWER_NODE_INDEX = CURRENT_FLOWER_NODE_INDEX->NEXT;
                        continue;
                }

                if (
                        targets_read_petal_data(
                                PETAL_PATH,
                                &PETAL
                        ) == ABNORMAL
                ) {
                        kui_add_line_and_render(
                                NOTICE_ERROR
                                "Failure to read "
                                RGB_COLOR_RICH_RED
                                "%s"
                                ANSI_COLOR_RESET
                                "."
                                , CURRENT_FLOWER_NODE_INDEX->TID
                        );
                        CURRENT_FLOWER_NODE_INDEX = CURRENT_FLOWER_NODE_INDEX->NEXT;
                        continue;
                }

                targets_buffer_path_stack_unsafe(
                        _prog_data,
                        MAX_PATH,
                        (char*)CURRENT_FLOWER_NODE_INDEX->TID,
                        TARGET_DIRECTORY
                );

                CURRENT_FLOWER_NODE_INDEX->PETAL = &PETAL;
                _prog_data->active_project_active_target = CURRENT_FLOWER_NODE_INDEX;
                _prog_data->active_project_active_target_directory = TARGET_DIRECTORY;

                kui_add_line_and_render(
                        NOTICE_INFO
                        "Pinging target "
                        ANSI_COLOR_CYAN
                        "%s"
                        ANSI_COLOR_RESET
                        "."
                        , CURRENT_FLOWER_NODE_INDEX->TID
                );

                ksping_single(_prog_data);

                CURRENT_FLOWER_NODE_INDEX->PETAL = PREVIOUS_PETAL;
                _prog_data->active_project_active_target = PREVIOUS_ACTIVE_TARGET;
                _prog_data->active_project_active_target_directory = PREVIOUS_ACTIVE_TARGET_DIRECTORY;

                CURRENT_FLOWER_NODE_INDEX = CURRENT_FLOWER_NODE_INDEX->NEXT;
        }

        _prog_data->active_project_active_target = PREVIOUS_ACTIVE_TARGET;
        _prog_data->active_project_active_target_directory = PREVIOUS_ACTIVE_TARGET_DIRECTORY;
}
