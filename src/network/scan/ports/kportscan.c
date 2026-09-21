// Copyright 2026 Jamison A. Drapeau
#define _POSIX_C_SOURCE 200809L

#include "kportscan.h"
#include "kbanner.h"
#include "kportscan_internal.h"
#include "kscan.h"
#include "kui.h"
#include "tlib.h"

#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>

static int kportscan_plan_set_port(
        uint8_t * BITMAP,
        uint32_t * COUNT,
        unsigned long PORT
) {
        size_t BYTE;
        uint8_t MASK;

        if (!BITMAP || !COUNT || PORT == 0 || PORT >= MAX_PORTS) {
                return ABNORMAL;
        }

        BYTE = (size_t)PORT >> 3;
        MASK = (uint8_t)(1U << (PORT & 7U));

        if ((BITMAP[BYTE] & MASK) == 0) {
                BITMAP[BYTE] |= MASK;
                (*COUNT)++;
        }

        return NORMAL;
}

int8_t kportscan_plan_has_port(
        const uint8_t * BITMAP,
        uint16_t PORT
) {
        size_t BYTE;
        uint8_t MASK;

        if (!BITMAP || PORT == 0) {
                return ISFALSE;
        }

        BYTE = (size_t)PORT >> 3;
        MASK = (uint8_t)(1U << (PORT & 7U));
        return (BITMAP[BYTE] & MASK) ? ISTRUE : ISFALSE;
}

static int kportscan_parse_number(
        const char * TEXT,
        unsigned long * VALUE
) {
        char * END = NULL;
        unsigned long PARSED;

        if (!TEXT || TEXT[0] == 0x00 || !VALUE) {
                return ABNORMAL;
        }

        errno = 0;
        PARSED = strtoul(TEXT, &END, 10);

        if (
                errno != 0
                || !END
                || *END != 0x00
                || PARSED == 0
                || PARSED >= MAX_PORTS
        ) {
                return ABNORMAL;
        }

        *VALUE = PARSED;
        return NORMAL;
}

static int kportscan_parse_segment(
        char * SEGMENT,
        uint8_t * BITMAP,
        uint32_t * COUNT
) {
        char * DASH;
        unsigned long FIRST;
        unsigned long LAST;

        if (!SEGMENT || SEGMENT[0] == 0x00) {
                return ABNORMAL;
        }

        DASH = strchr(SEGMENT, '-');

        if (!DASH) {
                if (kportscan_parse_number(SEGMENT, &FIRST) != NORMAL) {
                        return ABNORMAL;
                }
                return kportscan_plan_set_port(BITMAP, COUNT, FIRST);
        }

        if (strchr(DASH + 1, '-') != NULL) {
                return ABNORMAL;
        }

        *DASH = 0x00;

        if (
                kportscan_parse_number(SEGMENT, &FIRST) != NORMAL
                || kportscan_parse_number(DASH + 1, &LAST) != NORMAL
                || LAST < FIRST
        ) {
                return ABNORMAL;
        }

        for (unsigned long PORT = FIRST; PORT <= LAST; PORT++) {
                if (kportscan_plan_set_port(BITMAP, COUNT, PORT) != NORMAL) {
                        return ABNORMAL;
                }
        }

        return NORMAL;
}

static int kportscan_parse_expression(
        const char * EXPRESSION,
        uint8_t * BITMAP,
        uint32_t * COUNT
) {
        char * COPY;
        char * SAVE = NULL;
        char * SEGMENT;
        int STATUS = NORMAL;

        if (!EXPRESSION || !BITMAP || !COUNT) {
                return ABNORMAL;
        }

        COPY = strdup(EXPRESSION);
        if (!COPY) {
                return ABNORMAL;
        }

        SEGMENT = strtok_r(COPY, ",", &SAVE);
        if (!SEGMENT) {
                STATUS = ABNORMAL;
                goto CLEANUP;
        }

        while (SEGMENT) {
                if (
                        kportscan_parse_segment(
                                SEGMENT,
                                BITMAP,
                                COUNT
                        ) != NORMAL
                ) {
                        STATUS = ABNORMAL;
                        goto CLEANUP;
                }
                SEGMENT = strtok_r(NULL, ",", &SAVE);
        }

CLEANUP:
        free(COPY);
        return STATUS;
}

static int kportscan_parse_plan(
        _carry_forward * _prog_data,
        KPORTSCAN_PLAN * PLAN
) {
        enum {
                KPORTSCAN_PARSE_NONE = 0,
                KPORTSCAN_PARSE_TCP,
                KPORTSCAN_PARSE_UDP
        } MODE = KPORTSCAN_PARSE_NONE;

        int8_t EXPECT_VALUE = ISFALSE;
        int8_t SAW_VALUE = ISFALSE;

        if (
                !_prog_data
                || !PLAN
                || !_prog_data->cmd_tokens
                || _prog_data->cmd_tokens_count < 2
        ) {
                return ABNORMAL;
        }

        memset(PLAN, 0x00, sizeof(*PLAN));

        for (c_size_t INDEX = 1; INDEX < _prog_data->cmd_tokens_count; INDEX++) {
                const char * TOKEN = (const char *)_prog_data->cmd_tokens[INDEX];

                if (!TOKEN) {
                        return ABNORMAL;
                }

                if (strcmp(TOKEN, "-F") == MATCH) {
                        if (
                                PLAN->FULL == ISTRUE
                                || SAW_VALUE == ISTRUE
                                || EXPECT_VALUE == ISTRUE
                                || MODE != KPORTSCAN_PARSE_NONE
                        ) {
                                return ABNORMAL;
                        }
                        PLAN->FULL = ISTRUE;
                        continue;
                }

                if (
                        strcmp(TOKEN, "-t") == MATCH
                        || strcmp(TOKEN, "-u") == MATCH
                ) {
                        if (PLAN->FULL == ISTRUE || EXPECT_VALUE == ISTRUE) {
                                return ABNORMAL;
                        }

                        MODE = strcmp(TOKEN, "-t") == MATCH
                                ? KPORTSCAN_PARSE_TCP
                                : KPORTSCAN_PARSE_UDP;
                        EXPECT_VALUE = ISTRUE;
                        continue;
                }

                if (PLAN->FULL == ISTRUE || MODE == KPORTSCAN_PARSE_NONE) {
                        return ABNORMAL;
                }

                if (MODE == KPORTSCAN_PARSE_TCP) {
                        if (
                                kportscan_parse_expression(
                                        TOKEN,
                                        PLAN->TCP_PORTS,
                                        &PLAN->TCP_COUNT
                                ) != NORMAL
                        ) {
                                return ABNORMAL;
                        }
                } else {
                        if (
                                kportscan_parse_expression(
                                        TOKEN,
                                        PLAN->UDP_PORTS,
                                        &PLAN->UDP_COUNT
                                ) != NORMAL
                        ) {
                                return ABNORMAL;
                        }
                }

                EXPECT_VALUE = ISFALSE;
                SAW_VALUE = ISTRUE;
        }

        if (EXPECT_VALUE == ISTRUE) {
                return ABNORMAL;
        }

        if (PLAN->FULL == ISTRUE) {
                for (unsigned long PORT = 1; PORT < MAX_PORTS; PORT++) {
                        (void)kportscan_plan_set_port(
                                PLAN->TCP_PORTS,
                                &PLAN->TCP_COUNT,
                                PORT
                        );
                        (void)kportscan_plan_set_port(
                                PLAN->UDP_PORTS,
                                &PLAN->UDP_COUNT,
                                PORT
                        );
                }
                return NORMAL;
        }

        if (PLAN->TCP_COUNT == 0 && PLAN->UDP_COUNT == 0) {
                return ABNORMAL;
        }

        return NORMAL;
}

void kportscan_print_usage(void) {
        kui_add_line(
                "< Usage: scan [ -F | -t "
                ANSI_COLOR_CYAN
                "<ports>"
                ANSI_COLOR_RESET
                " | -u "
                ANSI_COLOR_CYAN
                "<ports>"
                ANSI_COLOR_RESET
                " ]"
        );
        kui_add_line(
                NOTICE_INFO
                "Ports may be single values, ranges, or comma-separated combinations."
        );
        kui_add_line(
                NOTICE_INFO
                "Examples: scan -t 22,80,443 | scan -t 1-1024 -u 53,123 | scan -F"
        );
}

uint32_t kportscan_window_limit(
        const _carry_forward * _prog_data
) {
        uint32_t LIMIT;

        if (!_prog_data) {
                return 1;
        }

        LIMIT = _prog_data->gprof.limit_max_pending_transactions;
        if (LIMIT == 0) {
                LIMIT = 1;
        }
        if (LIMIT > KPORTSCAN_WINDOW_HARD) {
                LIMIT = KPORTSCAN_WINDOW_HARD;
        }
        return LIMIT;
}

uint64_t kportscan_monotonic_ns(void) {
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

void kportscan_rate_wait(
        const _carry_forward * _prog_data,
        uint64_t * NEXT_TX_NS
) {
        uint64_t NOW;
        uint64_t INTERVAL;

        if (!_prog_data || !NEXT_TX_NS) {
                return;
        }

        if (_prog_data->gprof.limit_max_tx_out_rate_pps == 0) {
                return;
        }

        INTERVAL = 1000000000ULL
                / _prog_data->gprof.limit_max_tx_out_rate_pps;
        if (INTERVAL == 0) {
                INTERVAL = 1;
        }

        NOW = kportscan_monotonic_ns();

        if (*NEXT_TX_NS != 0 && NOW < *NEXT_TX_NS) {
                uint64_t WAIT = *NEXT_TX_NS - NOW;
                struct timespec SLEEP;

                SLEEP.tv_sec = (time_t)(WAIT / 1000000000ULL);
                SLEEP.tv_nsec = (long)(WAIT % 1000000000ULL);

                while (nanosleep(&SLEEP, &SLEEP) != NORMAL && errno == EINTR) {
                        ;
                }

                NOW = kportscan_monotonic_ns();
        }

        *NEXT_TX_NS = NOW + INTERVAL;
}

nosix_status_t kportscan_write_probe(
        _carry_forward * _prog_data,
        KPCAP * PCAP,
        const nosix_tx_packet_t * PACKET,
        uint64_t * NEXT_TX_NS,
        size_t * FRAME_LENGTH
) {
        uint8_t FRAME_DATA[MAX_BLOCK];
        size_t LENGTH = 0;
        nosix_status_t STATUS;
        nosix_tx_surface_t SURFACE;
        uint32_t LINKTYPE;

        if (!_prog_data || !PCAP || !PACKET || !NEXT_TX_NS) {
                return NOSIX_ERR_ARGUMENT;
        }

        memset(FRAME_DATA, 0x00, sizeof(FRAME_DATA));
        kportscan_rate_wait(_prog_data, NEXT_TX_NS);

        STATUS = kwire_scan_write(
                _prog_data,
                PACKET,
                FRAME_DATA,
                sizeof(FRAME_DATA),
                &LENGTH
        );

        if (STATUS != NOSIX_OK) {
                return STATUS;
        }

        SURFACE = kwire_scan_last_tx_surface(_prog_data);
        LINKTYPE = SURFACE == NOSIX_TX_SURFACE_IPV4_LOCAL
                ? KPCAP_LINKTYPE_RAW_IPV4
                : KPCAP_LINKTYPE_ETHERNET;

        if (
                kwire_pcap_set_network(PCAP, LINKTYPE) != NORMAL
                ||
                kwire_pcap_packet(
                        PCAP,
                        FRAME_DATA,
                        LENGTH,
                        LENGTH,
                        kscan_now_ns()
                ) != NORMAL
        ) {
                return NOSIX_ERR_SYSTEM;
        }

        if (FRAME_LENGTH) {
                *FRAME_LENGTH = LENGTH;
        }

        return NOSIX_OK;
}

static int kportscan_pcap_type(
        char * OUT,
        size_t OUT_SIZE,
        KPORTSCAN_PROTOCOL PROTOCOL,
        nosix_address_family_t FAMILY,
        uint16_t PORT
) {
        const char * NAME;
        char IP_VERSION;
        int WRITTEN;

        if (!OUT || OUT_SIZE == 0 || PORT == 0) {
                return ABNORMAL;
        }

        NAME = PROTOCOL == KPORTSCAN_PROTOCOL_TCP
                ? "TCP"
                : PROTOCOL == KPORTSCAN_PROTOCOL_UDP
                        ? "UDP"
                        : NULL;
        IP_VERSION = FAMILY == NOSIX_ADDRESS_IPV4
                ? '4'
                : FAMILY == NOSIX_ADDRESS_IPV6
                        ? '6'
                        : 0;

        if (!NAME || IP_VERSION == 0) {
                return ABNORMAL;
        }

        WRITTEN = snprintf(
                OUT,
                OUT_SIZE,
                "%sv%c-%u",
                NAME,
                IP_VERSION,
                PORT
        );

        return (
                WRITTEN >= 0
                && WRITTEN < (int)OUT_SIZE
        ) ? NORMAL : ABNORMAL;
}

int kportscan_pcap_open(
        _carry_forward * _prog_data,
        const unsigned char * TID,
        KPORTSCAN_PROTOCOL PROTOCOL,
        nosix_address_family_t FAMILY,
        KPORTSCAN_PENDING * PENDING,
        KPCAP * PCAP
) {
        char SCAN_TYPE[32];
        uint64_t TIMESTAMP_NS;

        if (!_prog_data || !TID || !PENDING || !PCAP) {
                return ABNORMAL;
        }

        memset(SCAN_TYPE, 0x00, sizeof(SCAN_TYPE));
        if (
                kportscan_pcap_type(
                        SCAN_TYPE,
                        sizeof(SCAN_TYPE),
                        PROTOCOL,
                        FAMILY,
                        PENDING->PORT
                ) != NORMAL
        ) {
                return ABNORMAL;
        }

        TIMESTAMP_NS = kscan_now_ns();
        if (TIMESTAMP_NS == 0) {
                return ABNORMAL;
        }

        if (
                kwire_pcap_open_detached(
                        _prog_data,
                        PCAP,
                        SCAN_TYPE,
                        TID,
                        TIMESTAMP_NS
                ) != NORMAL
        ) {
                return ABNORMAL;
        }

        PENDING->PCAP_TIMESTAMP_NS = TIMESTAMP_NS;
        return NORMAL;
}

int kportscan_pcap_accept(
        _carry_forward * _prog_data,
        const unsigned char * TID,
        KPORTSCAN_PROTOCOL PROTOCOL,
        nosix_address_family_t FAMILY,
        const KPORTSCAN_PENDING * PENDING,
        const nosix_capture_t * CAPTURE
) {
        char SCAN_TYPE[32];
        const char * NAME;

        if (
                !_prog_data
                || !TID
                || !PENDING
                || PENDING->PCAP_TIMESTAMP_NS == 0
                || !CAPTURE
        ) {
                return ABNORMAL;
        }

        memset(SCAN_TYPE, 0x00, sizeof(SCAN_TYPE));
        if (
                kportscan_pcap_type(
                        SCAN_TYPE,
                        sizeof(SCAN_TYPE),
                        PROTOCOL,
                        FAMILY,
                        PENDING->PORT
                ) != NORMAL
        ) {
                return ABNORMAL;
        }

        if (
                kwire_pcap_append_capture(
                        _prog_data,
                        SCAN_TYPE,
                        TID,
                        PENDING->PCAP_TIMESTAMP_NS,
                        CAPTURE
                ) == NORMAL
        ) {
                return NORMAL;
        }

        NAME = PROTOCOL == KPORTSCAN_PROTOCOL_TCP ? "TCP" : "UDP";
        kui_add_line_and_render(
                NOTICE_ERROR
                "Failed to append %s/%u packet capture for target "
                ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET ".",
                NAME,
                PENDING->PORT,
                TID
        );
        return ABNORMAL;
}

int kportscan_capture_l3(
        const nosix_capture_t * CAPTURE,
        nosix_address_family_t FAMILY,
        const uint8_t ** IP,
        size_t * IP_LENGTH
) {
        const uint8_t * FRAME;
        size_t LENGTH;
        size_t L2 = 14;
        uint16_t ETHERTYPE;
        uint16_t EXPECTED;

        if (!CAPTURE || !IP || !IP_LENGTH || !CAPTURE->frame.data) {
                return ABNORMAL;
        }

        FRAME = CAPTURE->frame.data;
        LENGTH = CAPTURE->frame.length;

        if (CAPTURE->flags & NOSIX_CAPTURE_IPV4_LOCAL) {
                if (FAMILY != NOSIX_ADDRESS_IPV4 || LENGTH < 20) {
                        return ABNORMAL;
                }
                *IP = FRAME;
                *IP_LENGTH = LENGTH;
                return NORMAL;
        }

        if (LENGTH < 14) {
                return ABNORMAL;
        }

        ETHERTYPE = (uint16_t)(
                ((uint16_t)FRAME[12] << 8)
                | FRAME[13]
        );

        if (ETHERTYPE == 0x8100 || ETHERTYPE == 0x88a8) {
                if (LENGTH < 18) {
                        return ABNORMAL;
                }
                ETHERTYPE = (uint16_t)(
                        ((uint16_t)FRAME[16] << 8)
                        | FRAME[17]
                );
                L2 = 18;
        }

        EXPECTED = FAMILY == NOSIX_ADDRESS_IPV4 ? 0x0800 : 0x86dd;

        if (ETHERTYPE != EXPECTED || LENGTH <= L2) {
                return ABNORMAL;
        }

        *IP = FRAME + L2;
        *IP_LENGTH = LENGTH - L2;
        return NORMAL;
}

int kportscan_address_matches(
        const uint8_t * BYTES,
        const char * ADDRESS,
        nosix_address_family_t FAMILY
) {
        uint8_t EXPECTED[16];
        int AF;
        size_t LENGTH;

        if (!BYTES || !ADDRESS) {
                return ISFALSE;
        }

        memset(EXPECTED, 0x00, sizeof(EXPECTED));

        AF = FAMILY == NOSIX_ADDRESS_IPV4 ? AF_INET : AF_INET6;
        LENGTH = FAMILY == NOSIX_ADDRESS_IPV4 ? 4 : 16;

        if (inet_pton(AF, ADDRESS, EXPECTED) != 1) {
                return ISFALSE;
        }

        return memcmp(BYTES, EXPECTED, LENGTH) == MATCH
                ? ISTRUE
                : ISFALSE;
}

const char * kportscan_state_string(KPORTSCAN_STATE STATE) {
        switch (STATE) {
                case KPORTSCAN_STATE_OPEN: return "OPEN";
                case KPORTSCAN_STATE_CLOSED: return "CLOSED";
                case KPORTSCAN_STATE_FILTERED: return "FILTERED";
                case KPORTSCAN_STATE_OPEN_FILTERED: return "OPEN|FILTERED";
                case KPORTSCAN_STATE_ERROR: return "ERROR";
                default: return "UNKNOWN";
        }
}

const char * kportscan_evidence_string(KPORTSCAN_EVIDENCE EVIDENCE) {
        switch (EVIDENCE) {
                case KPORTSCAN_EVIDENCE_TCP_SYN_ACK: return "SYN+ACK";
                case KPORTSCAN_EVIDENCE_TCP_RST: return "RST";
                case KPORTSCAN_EVIDENCE_UDP_REPLY: return "UDP_REPLY";
                case KPORTSCAN_EVIDENCE_ICMP_PORT_UNREACHABLE: return "ICMP_PORT_UNREACHABLE";
                case KPORTSCAN_EVIDENCE_ICMP_FILTERED: return "ICMP_FILTERED";
                case KPORTSCAN_EVIDENCE_TIMEOUT: return "TIMEOUT";
                case KPORTSCAN_EVIDENCE_NETWORK_ERROR: return "NETWORK_ERROR";
                default: return "NONE";
        }
}

const char * kportscan_nosix_status_string(nosix_status_t STATUS) {
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

void kportscan_summary_add(
        KPORTSCAN_SUMMARY * SUMMARY,
        KPORTSCAN_STATE STATE
) {
        if (!SUMMARY || STATE == KPORTSCAN_STATE_UNKNOWN) {
                return;
        }

        SUMMARY->SCANNED++;

        switch (STATE) {
                case KPORTSCAN_STATE_OPEN: SUMMARY->OPEN++; break;
                case KPORTSCAN_STATE_CLOSED: SUMMARY->CLOSED++; break;
                case KPORTSCAN_STATE_FILTERED: SUMMARY->FILTERED++; break;
                case KPORTSCAN_STATE_OPEN_FILTERED: SUMMARY->OPEN_FILTERED++; break;
                case KPORTSCAN_STATE_ERROR: SUMMARY->ERROR++; break;
                default: break;
        }
}

void kportscan_emit_result(
        const char * ADDRESS,
        nosix_address_family_t FAMILY,
        const KPORTSCAN_RESULT * RESULT,
        int8_t PRINT_NEGATIVE
) {
        const char * PROTOCOL;
        const char * FAMILY_NAME;

        if (!ADDRESS || !RESULT) {
                return;
        }

        PROTOCOL = RESULT->PROTOCOL == KPORTSCAN_PROTOCOL_TCP ? "TCP" : "UDP";
        FAMILY_NAME = FAMILY == NOSIX_ADDRESS_IPV4 ? "IPv4" : "IPv6";

        if (RESULT->STATE == KPORTSCAN_STATE_OPEN) {
                kui_add_line_and_render(
                        NOTICE_RECIEVE
                        "%s/%u " ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET
                        " from " ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET
                        " -> " RGB_COLOR_BRIGHT_GREEN "OPEN" ANSI_COLOR_RESET
                        " (%s)",
                        PROTOCOL,
                        RESULT->PORT,
                        kportscan_evidence_string((KPORTSCAN_EVIDENCE)RESULT->EVIDENCE),
                        ADDRESS,
                        FAMILY_NAME
                );
                return;
        }

        if (RESULT->STATE == KPORTSCAN_STATE_ERROR) {
                kui_add_line_and_render(
                        NOTICE_ERROR
                        "%s/%u on %s -> ERROR (%s)",
                        PROTOCOL,
                        RESULT->PORT,
                        ADDRESS,
                        kportscan_evidence_string((KPORTSCAN_EVIDENCE)RESULT->EVIDENCE)
                );
                return;
        }

        if (PRINT_NEGATIVE != ISTRUE) {
                return;
        }

        if (RESULT->STATE == KPORTSCAN_STATE_CLOSED) {
                kui_add_line_and_render(
                        NOTICE_RECIEVE
                        "%s/%u " ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET
                        " from " ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET
                        " -> CLOSED (%s)",
                        PROTOCOL,
                        RESULT->PORT,
                        kportscan_evidence_string((KPORTSCAN_EVIDENCE)RESULT->EVIDENCE),
                        ADDRESS,
                        FAMILY_NAME
                );
                return;
        }

        kui_add_line_and_render(
                NOTICE_WARNING
                "%s/%u on " ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET
                " -> %s (%s, %s)",
                PROTOCOL,
                RESULT->PORT,
                ADDRESS,
                kportscan_state_string((KPORTSCAN_STATE)RESULT->STATE),
                kportscan_evidence_string((KPORTSCAN_EVIDENCE)RESULT->EVIDENCE),
                FAMILY_NAME
        );
}

int kportscan_store_result(
        _carry_forward * _prog_data,
        const unsigned char * TID,
        const KPORTSCAN_RESULT * RESULT
) {
        char PATH[MAX_PATH];
        FILE * FILE_HANDLE;
        long SIZE;
        const char * PROTOCOL;

        if (!_prog_data || !TID || !RESULT) {
                return ABNORMAL;
        }

        memset(PATH, 0x00, sizeof(PATH));

        if (
                snprintf(
                        PATH,
                        sizeof(PATH),
                        "%s/%s%s/%s/.ports",
                        _prog_data->wd,
                        KAMI_USER_PROJECTS_DIR,
                        _prog_data->active_project,
                        TID
                ) >= (int)sizeof(PATH)
        ) {
                return ABNORMAL;
        }

        FILE_HANDLE = fopen(PATH, "a+");
        if (!FILE_HANDLE) {
                return ABNORMAL;
        }

        if (fseek(FILE_HANDLE, 0, SEEK_END) != NORMAL) {
                fclose(FILE_HANDLE);
                return ABNORMAL;
        }

        SIZE = ftell(FILE_HANDLE);
        if (SIZE < 0) {
                fclose(FILE_HANDLE);
                return ABNORMAL;
        }

        if (SIZE == 0) {
                fprintf(
                        FILE_HANDLE,
                        "# KAMINOWAKU PORTS v%u\n"
                        "# timestamp_ns\tprotocol\taf\tport\tstate\tevidence\trx_bytes\n",
                        KPORTSCAN_STORE_VERSION
                );
        }

        PROTOCOL = RESULT->PROTOCOL == KPORTSCAN_PROTOCOL_TCP ? "TCP" : "UDP";

        if (
                fprintf(
                        FILE_HANDLE,
                        "%llu\t%s\t%u\t%u\t%s\t%s\t%u\n",
                        (unsigned long long)RESULT->TIMESTAMP_NS,
                        PROTOCOL,
                        RESULT->ADDRESS_FAMILY,
                        RESULT->PORT,
                        kportscan_state_string((KPORTSCAN_STATE)RESULT->STATE),
                        kportscan_evidence_string((KPORTSCAN_EVIDENCE)RESULT->EVIDENCE),
                        RESULT->RX_BYTES
                ) < 0
        ) {
                fclose(FILE_HANDLE);
                return ABNORMAL;
        }

        fflush(FILE_HANDLE);
        fclose(FILE_HANDLE);
        return NORMAL;
}

static void kportscan_print_summary(
        const char * NAME,
        const KPORTSCAN_SUMMARY * SUMMARY
) {
        if (!NAME || !SUMMARY || SUMMARY->SCANNED == 0) {
                return;
        }

        if (strcmp(NAME, "TCP") == MATCH) {
                kui_add_line_and_render(
                        NOTICE_INFO
                        "TCP: " RGB_COLOR_BRIGHT_GREEN "%llu open" ANSI_COLOR_RESET
                        " / %llu closed / %llu filtered / %llu error / %llu scanned.",
                        (unsigned long long)SUMMARY->OPEN,
                        (unsigned long long)SUMMARY->CLOSED,
                        (unsigned long long)SUMMARY->FILTERED,
                        (unsigned long long)SUMMARY->ERROR,
                        (unsigned long long)SUMMARY->SCANNED
                );
                return;
        }

        kui_add_line_and_render(
                NOTICE_INFO
                "UDP: " RGB_COLOR_BRIGHT_GREEN "%llu open" ANSI_COLOR_RESET
                " / %llu closed / %llu filtered / %llu open|filtered / %llu error / %llu scanned.",
                (unsigned long long)SUMMARY->OPEN,
                (unsigned long long)SUMMARY->CLOSED,
                (unsigned long long)SUMMARY->FILTERED,
                (unsigned long long)SUMMARY->OPEN_FILTERED,
                (unsigned long long)SUMMARY->ERROR,
                (unsigned long long)SUMMARY->SCANNED
        );
}

static int kportscan_scan_target(
        _carry_forward * _prog_data,
        const unsigned char * TID,
        TARGET * PETAL,
        const KPORTSCAN_PLAN * PLAN,
        int8_t PRINT_NEGATIVE
) {
        KPORTSCAN_SUMMARY TCP_SUMMARY;
        KPORTSCAN_SUMMARY UDP_SUMMARY;
        int8_t ADDRESS_FOUND = ISFALSE;
        int STATUS = NORMAL;
        char PETAL_PATH[MAX_PATH];
        uint64_t SCAN_TIME;
        TARGET_SCAN_DATA PREVIOUS_SCAN;

        if (!_prog_data || !TID || !PETAL || !PLAN) {
                return ABNORMAL;
        }

        memset(&TCP_SUMMARY, 0x00, sizeof(TCP_SUMMARY));
        memset(&UDP_SUMMARY, 0x00, sizeof(UDP_SUMMARY));

        kui_add_line_and_render(
                NOTICE_INFO
                "Scanning target " ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET ".",
                TID
        );

        if (PETAL->IPV4[0] != 0x00) {
                ADDRESS_FOUND = ISTRUE;
                kui_add_line_and_render(
                        NOTICE_INFO
                        "IPv4 " ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET,
                        PETAL->IPV4
                );

                if (PLAN->TCP_COUNT > 0) {
                        uint64_t TCP_SCAN_START_NS = kscan_now_ns();

                        if (
                                kportscan_tcp(
                                        _prog_data,
                                        TID,
                                        PETAL,
                                        (const char *)PETAL->IPV4,
                                        NOSIX_ADDRESS_IPV4,
                                        PLAN,
                                        &TCP_SUMMARY,
                                        PRINT_NEGATIVE
                                ) != NORMAL
                        ) {
                                STATUS = ABNORMAL;
                        }

                        (void)kbanner_enrich_tcp(
                                _prog_data,
                                TID,
                                (const char *)PETAL->IPV4,
                                NOSIX_ADDRESS_IPV4,
                                TCP_SCAN_START_NS
                        );
                }

                if (
                        PLAN->UDP_COUNT > 0
                        && kportscan_udp(
                                _prog_data,
                                TID,
                                (const char *)PETAL->IPV4,
                                NOSIX_ADDRESS_IPV4,
                                PLAN,
                                &UDP_SUMMARY,
                                PRINT_NEGATIVE
                        ) != NORMAL
                ) {
                        STATUS = ABNORMAL;
                }
        }

        if (PETAL->IPV6[0] != 0x00) {
                ADDRESS_FOUND = ISTRUE;
                kui_add_line_and_render(
                        NOTICE_INFO
                        "IPv6 " ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET,
                        PETAL->IPV6
                );

                if (PLAN->TCP_COUNT > 0) {
                        uint64_t TCP_SCAN_START_NS = kscan_now_ns();

                        if (
                                kportscan_tcp(
                                        _prog_data,
                                        TID,
                                        PETAL,
                                        (const char *)PETAL->IPV6,
                                        NOSIX_ADDRESS_IPV6,
                                        PLAN,
                                        &TCP_SUMMARY,
                                        PRINT_NEGATIVE
                                ) != NORMAL
                        ) {
                                STATUS = ABNORMAL;
                        }

                        (void)kbanner_enrich_tcp(
                                _prog_data,
                                TID,
                                (const char *)PETAL->IPV6,
                                NOSIX_ADDRESS_IPV6,
                                TCP_SCAN_START_NS
                        );
                }

                if (
                        PLAN->UDP_COUNT > 0
                        && kportscan_udp(
                                _prog_data,
                                TID,
                                (const char *)PETAL->IPV6,
                                NOSIX_ADDRESS_IPV6,
                                PLAN,
                                &UDP_SUMMARY,
                                PRINT_NEGATIVE
                        ) != NORMAL
                ) {
                        STATUS = ABNORMAL;
                }
        }

        if (ADDRESS_FOUND != ISTRUE) {
                kui_add_line_and_render(
                        NOTICE_WARNING
                        "Target " ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET
                        " has no IPv4 or IPv6 address.",
                        TID
                );
                return ABNORMAL;
        }

        kportscan_print_summary("TCP", &TCP_SUMMARY);
        kportscan_print_summary("UDP", &UDP_SUMMARY);

        SCAN_TIME = kscan_now_ns();
        PREVIOUS_SCAN = PETAL->SCAN;

        if (PETAL->SCAN.SCAN_VERSION != TARGET_SCAN_DATA_VERSION) {
                memset(&PETAL->SCAN, 0x00, sizeof(PETAL->SCAN));
                PETAL->SCAN.SCAN_VERSION = TARGET_SCAN_DATA_VERSION;
        }

        PETAL->SCAN.LAST_SCAN_NS = SCAN_TIME;

        memset(PETAL_PATH, 0x00, sizeof(PETAL_PATH));

        if (
                build_petal_path(
                        PETAL_PATH,
                        sizeof(PETAL_PATH),
                        _prog_data->wd,
                        _prog_data->active_project,
                        TID
                ) != NORMAL
                || targets_write_petal_data(PETAL_PATH, PETAL) != NORMAL
        ) {
                PETAL->SCAN = PREVIOUS_SCAN;
                kui_add_line_and_render(
                        NOTICE_ERROR
                        "Failed to persist target scan timestamp for "
                        ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET ".",
                        TID
                );
                STATUS = ABNORMAL;
        } else {
                _prog_data->active_project_has_been_scanned = ISTRUE;

                if (SCAN_TIME > _prog_data->active_project_last_scan_ns) {
                        _prog_data->active_project_last_scan_ns = SCAN_TIME;
                }
        }

        return STATUS;
}

void kportscan_run(_carry_forward * _prog_data) {
        KPORTSCAN_PLAN PLAN;
        uint64_t TOTAL_REQUESTED;
        int8_t PRINT_NEGATIVE;

        if (!_prog_data) {
                return;
        }

        if (kportscan_parse_plan(_prog_data, &PLAN) != NORMAL) {
                kportscan_print_usage();
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
                        "Port scanning requires " ANSI_COLOR_CYAN
                        "tx.checksum_mode=auto" ANSI_COLOR_RESET
                        " so NOSIX can finalize TCP/UDP checksums."
                );
                return;
        }

        TOTAL_REQUESTED = (uint64_t)PLAN.TCP_COUNT + (uint64_t)PLAN.UDP_COUNT;
        PRINT_NEGATIVE = TOTAL_REQUESTED <= 128 ? ISTRUE : ISFALSE;

        kui_add_line_and_render(
                NOTICE_INFO
                "Scan plan: TCP=" ANSI_COLOR_CYAN "%u" ANSI_COLOR_RESET
                " UDP=" ANSI_COLOR_CYAN "%u" ANSI_COLOR_RESET
                "%s.",
                PLAN.TCP_COUNT,
                PLAN.UDP_COUNT,
                PLAN.FULL == ISTRUE ? " [FULL]" : ""
        );

        if (
                _prog_data->active_project_active_target_context == ISTRUE
                && _prog_data->active_project_active_target
                && _prog_data->active_project_active_target->PETAL
        ) {
                (void)kportscan_scan_target(
                        _prog_data,
                        _prog_data->active_project_active_target->TID,
                        _prog_data->active_project_active_target->PETAL,
                        &PLAN,
                        PRINT_NEGATIVE
                );
                return;
        }

        FLOWER * NODE = _prog_data->active_project_flower
                ? _prog_data->active_project_flower->NEXT
                : NULL;

        while (NODE) {
                TARGET PETAL;
                char PETAL_PATH[MAX_PATH];

                kui_progress_advance();

                memset(&PETAL, 0x00, sizeof(PETAL));
                memset(PETAL_PATH, 0x00, sizeof(PETAL_PATH));

                if (
                        build_petal_path(
                                PETAL_PATH,
                                sizeof(PETAL_PATH),
                                _prog_data->wd,
                                _prog_data->active_project,
                                NODE->TID
                        ) != NORMAL
                        || targets_read_petal_data(PETAL_PATH, &PETAL) != NORMAL
                ) {
                        kui_add_line_and_render(
                                NOTICE_ERROR
                                "Failed to load target " ANSI_COLOR_CYAN
                                "%s" ANSI_COLOR_RESET " for scanning.",
                                NODE->TID
                        );
                        NODE = NODE->NEXT;
                        continue;
                }

                (void)kportscan_scan_target(
                        _prog_data,
                        NODE->TID,
                        &PETAL,
                        &PLAN,
                        PRINT_NEGATIVE
                );

                NODE = NODE->NEXT;
        }
}
