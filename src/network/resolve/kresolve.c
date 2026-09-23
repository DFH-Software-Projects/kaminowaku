// Copyright 2026 Jamison A. Drapeau
#define _POSIX_C_SOURCE 200809L
#include "kresolve.h"
#include <nosix.h>
#include "kwire.h"
#include "kui.h"
#include "tlib.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>

#define DNS_PORT             53U
#define DNS_PACKET_MAX       512U
#define DNS_UDP_HEADER_SIZE  8U
#define DNS_WIRE_MAX         (DNS_PACKET_MAX + DNS_UDP_HEADER_SIZE)
#define DNS_A                1U
#define DNS_AAAA             28U
#define DNS_IN               1U

#define RESOLVE_OK           0
#define RESOLVE_NOT_FOUND    1
#define RESOLVE_TIMEOUT      2
#define RESOLVE_ERROR       -1

static uint16_t DNS_ID = 0;
static uint16_t DNS_SOURCE_PORT = 49151U;

static uint16_t read_u16(const uint8_t *P) {
        return (uint16_t)(((uint16_t)P[0] << 8) | P[1]);
}

static void write_u16(uint8_t *P, uint16_t VALUE) {
        P[0] = (uint8_t)(VALUE >> 8);
        P[1] = (uint8_t)(VALUE & 0xffU);
}

static uint16_t dns_next_id(void) {
        DNS_ID++;
        if (DNS_ID == 0) {
                DNS_ID = 1;
        }
        return DNS_ID;
}

static uint16_t dns_next_source_port(void) {
        DNS_SOURCE_PORT++;
        if (DNS_SOURCE_PORT < 49152U) {
                DNS_SOURCE_PORT = 49152U;
        }
        return DNS_SOURCE_PORT;
}

static double dns_elapsed_ms(
        const struct timespec *START,
        const struct timespec *END
) {
        double SECONDS;
        double NANOSECONDS;

        SECONDS = (double)(END->tv_sec - START->tv_sec) * 1000.0;
        NANOSECONDS = (double)(END->tv_nsec - START->tv_nsec) / 1000000.0;
        return SECONDS + NANOSECONDS;
}

static const char * kresolve_nosix_status_string(nosix_status_t STATUS) {
        switch (STATUS) {
                case NOSIX_OK:                  return "NOSIX_OK";
                case NOSIX_TIMEOUT:             return "NOSIX_TIMEOUT";
                case NOSIX_TRUNCATED:           return "NOSIX_TRUNCATED";
                case NOSIX_ERR_ARGUMENT:        return "NOSIX_ERR_ARGUMENT";
                case NOSIX_ERR_STATE:           return "NOSIX_ERR_STATE";
                case NOSIX_ERR_MEMORY:          return "NOSIX_ERR_MEMORY";
                case NOSIX_ERR_SYSTEM:          return "NOSIX_ERR_SYSTEM";
                case NOSIX_ERR_ADDRESS:         return "NOSIX_ERR_ADDRESS";
                case NOSIX_ERR_FRAME_TOO_LARGE: return "NOSIX_ERR_FRAME_TOO_LARGE";
                case NOSIX_ERR_UNSUPPORTED:     return "NOSIX_ERR_UNSUPPORTED";
                case NOSIX_ERR_ROUTE:           return "NOSIX_ERR_ROUTE";
                case NOSIX_ERR_NEIGHBOR:        return "NOSIX_ERR_NEIGHBOR";
        }
        return "NOSIX_UNKNOWN";
}

static int hostname_from_url(
        const unsigned char *URL,
        char *OUT,
        size_t OUT_SIZE
) {
        const char *START;
        const char *END;
        const char *SCHEME;
        size_t LENGTH;

        if (!URL || !OUT || OUT_SIZE == 0) {
                return ABNORMAL;
        }

        START = (const char*)URL;
        while (*START == ' ' || *START == '\t') {
                START++;
        }

        SCHEME = strstr(START, "://");
        if (SCHEME) {
                START = SCHEME + 3;
        }

        if (*START == '[') {
                START++;
                END = strchr(START, ']');
                if (!END) {
                        return ABNORMAL;
                }
        } else {
                END = START;
                while (
                        *END
                        && *END != '/'
                        && *END != ':'
                        && *END != '?'
                        && *END != '#'
                        && *END != ' '
                        && *END != '\t'
                ) {
                        END++;
                }
        }

        LENGTH = (size_t)(END - START);
        if (LENGTH == 0 || LENGTH >= OUT_SIZE) {
                return ABNORMAL;
        }

        memcpy(OUT, START, LENGTH);
        OUT[LENGTH] = 0x00;

        while (LENGTH > 0 && OUT[LENGTH - 1] == '.') {
                OUT[--LENGTH] = 0x00;
        }

        return LENGTH ? NORMAL : ABNORMAL;
}

static int apply_search_domain(
        const GLOBAL_PROFILE *GPROF,
        char *HOST,
        size_t HOST_SIZE
) {
        size_t HOST_LENGTH;
        size_t DOMAIN_LENGTH;

        if (!GPROF || !HOST || HOST[0] == 0x00) {
                return ABNORMAL;
        }

        if (
                strchr(HOST, '.')
                || GPROF->dns_search_domain[0] == 0x00
        ) {
                return NORMAL;
        }

        HOST_LENGTH = strlen(HOST);
        DOMAIN_LENGTH = strlen((const char*)GPROF->dns_search_domain);

        if (HOST_LENGTH + DOMAIN_LENGTH + 2 > HOST_SIZE) {
                return ABNORMAL;
        }

        HOST[HOST_LENGTH++] = '.';
        memcpy(
                HOST + HOST_LENGTH,
                GPROF->dns_search_domain,
                DOMAIN_LENGTH + 1
        );

        return NORMAL;
}

static int encode_name(
        const char *HOST,
        uint8_t *OUT,
        size_t OUT_SIZE,
        size_t *USED
) {
        const char *LABEL;
        size_t POSITION;

        if (!HOST || !OUT || !USED) {
                return ABNORMAL;
        }

        LABEL = HOST;
        POSITION = 0;

        while (*LABEL) {
                const char *DOT = strchr(LABEL, '.');
                size_t LENGTH = DOT
                        ? (size_t)(DOT - LABEL)
                        : strlen(LABEL);

                if (
                        LENGTH == 0
                        || LENGTH > 63
                        || POSITION + LENGTH + 1 >= OUT_SIZE
                ) {
                        return ABNORMAL;
                }

                OUT[POSITION++] = (uint8_t)LENGTH;
                memcpy(OUT + POSITION, LABEL, LENGTH);
                POSITION += LENGTH;

                if (!DOT) {
                        break;
                }

                LABEL = DOT + 1;
        }

        if (POSITION >= OUT_SIZE) {
                return ABNORMAL;
        }

        OUT[POSITION++] = 0x00;
        *USED = POSITION;
        return NORMAL;
}

static int build_dns_query(
        const char *HOST,
        uint16_t TYPE,
        uint16_t ID,
        uint8_t *OUT,
        size_t OUT_SIZE,
        size_t *QUERY_LENGTH
) {
        size_t QNAME_LENGTH;
        size_t LENGTH;

        if (!HOST || !OUT || !QUERY_LENGTH || OUT_SIZE < 17) {
                return ABNORMAL;
        }

        memset(OUT, 0x00, OUT_SIZE);

        write_u16(OUT, ID);
        write_u16(OUT + 2, 0x0100U);
        write_u16(OUT + 4, 1U);

        if (
                encode_name(
                        HOST,
                        OUT + 12,
                        OUT_SIZE - 12,
                        &QNAME_LENGTH
                ) != NORMAL
        ) {
                return ABNORMAL;
        }

        LENGTH = 12 + QNAME_LENGTH + 4;
        if (LENGTH > OUT_SIZE) {
                return ABNORMAL;
        }

        write_u16(OUT + 12 + QNAME_LENGTH, TYPE);
        write_u16(OUT + 14 + QNAME_LENGTH, DNS_IN);

        *QUERY_LENGTH = LENGTH;
        return NORMAL;
}

static int build_udp_dns_query(
        const char *HOST,
        uint16_t TYPE,
        uint16_t ID,
        uint16_t SOURCE_PORT,
        uint8_t *OUT,
        size_t OUT_SIZE,
        size_t *WIRE_LENGTH
) {
        size_t DNS_LENGTH;
        size_t UDP_LENGTH;

        if (
                !OUT
                || !WIRE_LENGTH
                || OUT_SIZE < DNS_UDP_HEADER_SIZE + 17
        ) {
                return ABNORMAL;
        }

        memset(OUT, 0x00, OUT_SIZE);

        if (
                build_dns_query(
                        HOST,
                        TYPE,
                        ID,
                        OUT + DNS_UDP_HEADER_SIZE,
                        OUT_SIZE - DNS_UDP_HEADER_SIZE,
                        &DNS_LENGTH
                ) != NORMAL
        ) {
                return ABNORMAL;
        }

        UDP_LENGTH = DNS_UDP_HEADER_SIZE + DNS_LENGTH;
        if (UDP_LENGTH > UINT16_MAX) {
                return ABNORMAL;
        }

        write_u16(OUT, SOURCE_PORT);
        write_u16(OUT + 2, DNS_PORT);
        write_u16(OUT + 4, (uint16_t)UDP_LENGTH);

        // Checksum starts at zero. NOSIX owns checksum finalization when
        // tx.checksum_mode=auto and preserves caller bytes in manual mode.
        write_u16(OUT + 6, 0x0000U);

        *WIRE_LENGTH = UDP_LENGTH;
        return NORMAL;
}

static int skip_name(
        const uint8_t *PACKET,
        size_t PACKET_LENGTH,
        size_t *OFFSET
) {
        size_t POSITION;

        if (!PACKET || !OFFSET) {
                return ABNORMAL;
        }

        POSITION = *OFFSET;

        while (POSITION < PACKET_LENGTH) {
                uint8_t LENGTH = PACKET[POSITION];

                if (LENGTH == 0) {
                        *OFFSET = POSITION + 1;
                        return NORMAL;
                }

                if ((LENGTH & 0xc0U) == 0xc0U) {
                        if (POSITION + 1 >= PACKET_LENGTH) {
                                return ABNORMAL;
                        }

                        *OFFSET = POSITION + 2;
                        return NORMAL;
                }

                if (
                        (LENGTH & 0xc0U) != 0
                        || LENGTH > 63
                        || POSITION + 1 + LENGTH > PACKET_LENGTH
                ) {
                        return ABNORMAL;
                }

                POSITION += 1 + LENGTH;
        }

        return ABNORMAL;
}

static int parse_answer(
        const uint8_t *PACKET,
        size_t PACKET_LENGTH,
        uint16_t ID,
        uint16_t TYPE,
        char *ADDRESS,
        size_t ADDRESS_SIZE
) {
        uint16_t FLAGS;
        uint16_t QUESTION_COUNT;
        uint16_t ANSWER_COUNT;
        size_t OFFSET;

        if (
                !PACKET
                || !ADDRESS
                || ADDRESS_SIZE == 0
                || PACKET_LENGTH < 12
                || read_u16(PACKET) != ID
        ) {
                return RESOLVE_ERROR;
        }

        FLAGS = read_u16(PACKET + 2);

        if (!(FLAGS & 0x8000U)) {
                return RESOLVE_ERROR;
        }

        if (FLAGS & 0x0200U) {
                return RESOLVE_ERROR;
        }

        if ((FLAGS & 0x000fU) == 3U) {
                return RESOLVE_NOT_FOUND;
        }

        if ((FLAGS & 0x000fU) != 0U) {
                return RESOLVE_ERROR;
        }

        QUESTION_COUNT = read_u16(PACKET + 4);
        ANSWER_COUNT = read_u16(PACKET + 6);
        OFFSET = 12;

        for (uint16_t INDEX = 0; INDEX < QUESTION_COUNT; INDEX++) {
                if (
                        skip_name(
                                PACKET,
                                PACKET_LENGTH,
                                &OFFSET
                        ) != NORMAL
                        || OFFSET + 4 > PACKET_LENGTH
                ) {
                        return RESOLVE_ERROR;
                }

                OFFSET += 4;
        }

        for (uint16_t INDEX = 0; INDEX < ANSWER_COUNT; INDEX++) {
                uint16_t RR_TYPE;
                uint16_t RR_CLASS;
                uint16_t RDATA_LENGTH;
                int FAMILY;
                size_t EXPECTED_LENGTH;

                if (
                        skip_name(
                                PACKET,
                                PACKET_LENGTH,
                                &OFFSET
                        ) != NORMAL
                        || OFFSET + 10 > PACKET_LENGTH
                ) {
                        return RESOLVE_ERROR;
                }

                RR_TYPE = read_u16(PACKET + OFFSET);
                RR_CLASS = read_u16(PACKET + OFFSET + 2);
                RDATA_LENGTH = read_u16(PACKET + OFFSET + 8);
                OFFSET += 10;

                if (OFFSET + RDATA_LENGTH > PACKET_LENGTH) {
                        return RESOLVE_ERROR;
                }

                FAMILY = TYPE == DNS_A ? AF_INET : AF_INET6;
                EXPECTED_LENGTH = TYPE == DNS_A ? 4U : 16U;

                if (
                        RR_CLASS == DNS_IN
                        && RR_TYPE == TYPE
                        && RDATA_LENGTH == EXPECTED_LENGTH
                ) {
                        if (
                                inet_ntop(
                                        FAMILY,
                                        PACKET + OFFSET,
                                        ADDRESS,
                                        ADDRESS_SIZE
                                )
                        ) {
                                return RESOLVE_OK;
                        }
                }

                OFFSET += RDATA_LENGTH;
        }

        return RESOLVE_NOT_FOUND;
}

static int capture_dns_payload_ipv4(
        const nosix_capture_t *CAPTURE,
        const uint8_t SERVER_IPV4[4],
        uint16_t SOURCE_PORT,
        const uint8_t **DNS_PAYLOAD,
        size_t *DNS_LENGTH
) {
        const uint8_t *FRAME;
        const uint8_t *IPV4;
        const uint8_t *UDP;
        size_t L2_HEADER_LENGTH;
        size_t IPV4_HEADER_LENGTH;
        size_t CAPTURED_IPV4_LENGTH;
        size_t CAPTURED_UDP_LENGTH;
        uint16_t ETHERTYPE;
        uint16_t IPV4_TOTAL_LENGTH;
        uint16_t UDP_LENGTH;

        if (
                !CAPTURE
                || !SERVER_IPV4
                || !DNS_PAYLOAD
                || !DNS_LENGTH
                || !CAPTURE->frame.data
        ) {
                return ABNORMAL;
        }

        FRAME = CAPTURE->frame.data;
        L2_HEADER_LENGTH = 0;

        if (CAPTURE->flags & NOSIX_CAPTURE_IPV4_LOCAL) {
                if (CAPTURE->frame.length < 28) {
                        return ABNORMAL;
                }
        } else {
                if (CAPTURE->frame.length < 42) {
                        return ABNORMAL;
                }

                L2_HEADER_LENGTH = 14;
                ETHERTYPE = read_u16(FRAME + 12);

                if (
                        ETHERTYPE == 0x8100U
                        || ETHERTYPE == 0x88A8U
                ) {
                        if (CAPTURE->frame.length < 46) {
                                return ABNORMAL;
                        }

                        ETHERTYPE = read_u16(FRAME + 16);
                        L2_HEADER_LENGTH = 18;
                }

                if (ETHERTYPE != 0x0800U) {
                        return ABNORMAL;
                }
        }

        IPV4 = FRAME + L2_HEADER_LENGTH;

        if ((IPV4[0] >> 4) != 4) {
                return ABNORMAL;
        }

        IPV4_HEADER_LENGTH = (size_t)(IPV4[0] & 0x0fU) * 4U;
        if (IPV4_HEADER_LENGTH < 20) {
                return ABNORMAL;
        }

        if (
                CAPTURE->frame.length
                < L2_HEADER_LENGTH + IPV4_HEADER_LENGTH + DNS_UDP_HEADER_SIZE
        ) {
                return ABNORMAL;
        }

        if (IPV4[9] != NOSIX_IPPROTO_UDP) {
                return ABNORMAL;
        }

        // Ignore fragmented DNS replies in this first UDP path.
        if (
                (IPV4[6] & 0x3fU) != 0
                || IPV4[7] != 0
        ) {
                return ABNORMAL;
        }

        if (memcmp(IPV4 + 12, SERVER_IPV4, 4) != MATCH) {
                return ABNORMAL;
        }

        IPV4_TOTAL_LENGTH = read_u16(IPV4 + 2);
        if (
                IPV4_TOTAL_LENGTH
                < IPV4_HEADER_LENGTH + DNS_UDP_HEADER_SIZE
        ) {
                return ABNORMAL;
        }

        CAPTURED_IPV4_LENGTH = CAPTURE->frame.length - L2_HEADER_LENGTH;
        if (IPV4_TOTAL_LENGTH > CAPTURED_IPV4_LENGTH) {
                IPV4_TOTAL_LENGTH = (uint16_t)CAPTURED_IPV4_LENGTH;
        }

        UDP = IPV4 + IPV4_HEADER_LENGTH;

        if (read_u16(UDP) != DNS_PORT) {
                return ABNORMAL;
        }

        if (read_u16(UDP + 2) != SOURCE_PORT) {
                return ABNORMAL;
        }

        UDP_LENGTH = read_u16(UDP + 4);
        if (UDP_LENGTH < DNS_UDP_HEADER_SIZE) {
                return ABNORMAL;
        }

        CAPTURED_UDP_LENGTH = IPV4_TOTAL_LENGTH - IPV4_HEADER_LENGTH;
        if (UDP_LENGTH > CAPTURED_UDP_LENGTH) {
                UDP_LENGTH = (uint16_t)CAPTURED_UDP_LENGTH;
        }

        if (UDP_LENGTH <= DNS_UDP_HEADER_SIZE) {
                return ABNORMAL;
        }

        *DNS_PAYLOAD = UDP + DNS_UDP_HEADER_SIZE;
        *DNS_LENGTH = UDP_LENGTH - DNS_UDP_HEADER_SIZE;
        return NORMAL;
}

static int dns_query(
        _carry_forward * _prog_data,
        KPCAP *PCAP,
        const char *SERVER,
        const char *HOST,
        uint16_t TYPE,
        int TIMEOUT_MS,
        char *ADDRESS,
        size_t ADDRESS_SIZE
) {
        uint8_t THROW[DNS_WIRE_MAX];
        uint8_t CATCH[OUT_BLOCK];
        uint8_t SERVER_IPV4[4];
        nosix_tx_packet_t PACKET;
        nosix_capture_t CAPTURE;
        nosix_status_t STATUS;
        struct timespec TIME_START;
        struct timespec TIME_NOW;
        KWIRE_DEADLINE RX_DEADLINE;
        const uint8_t *DNS_PAYLOAD;
        size_t DNS_PAYLOAD_LENGTH;
        size_t THROW_LENGTH;
        size_t TX_FRAME_LENGTH;
        uint16_t ID;
        uint16_t SOURCE_PORT;

        if (
                !_prog_data
                || !PCAP
                || !SERVER
                || !HOST
                || !ADDRESS
                || ADDRESS_SIZE == 0
                || !_prog_data->nosix_net
        ) {
                return RESOLVE_ERROR;
        }

        memset(THROW, 0x00, sizeof(THROW));
        memset(CATCH, 0x00, sizeof(CATCH));
        memset(SERVER_IPV4, 0x00, sizeof(SERVER_IPV4));
        memset(&PACKET, 0x00, sizeof(PACKET));
        memset(&CAPTURE, 0x00, sizeof(CAPTURE));
        memset(&TIME_START, 0x00, sizeof(TIME_START));
        memset(&TIME_NOW, 0x00, sizeof(TIME_NOW));

        if (inet_pton(AF_INET, SERVER, SERVER_IPV4) != 1) {
                return RESOLVE_ERROR;
        }

        ID = dns_next_id();
        SOURCE_PORT = dns_next_source_port();

        if (
                build_udp_dns_query(
                        HOST,
                        TYPE,
                        ID,
                        SOURCE_PORT,
                        THROW,
                        sizeof(THROW),
                        &THROW_LENGTH
                ) != NORMAL
        ) {
                return RESOLVE_ERROR;
        }

        PACKET.destination.family = NOSIX_ADDRESS_IPV4;
        memcpy(
                PACKET.destination.bytes.ipv4,
                SERVER_IPV4,
                sizeof(SERVER_IPV4)
        );
        PACKET.ip_protocol = NOSIX_IPPROTO_UDP;
        PACKET.upper_layer = THROW;
        PACKET.upper_layer_length = THROW_LENGTH;
        PACKET.flags = 0;

        CAPTURE.frame.data = CATCH;
        CAPTURE.frame.capacity = sizeof(CATCH);

        if (TIMEOUT_MS < 1) {
                TIMEOUT_MS = 1;
        }

        if (kwire_deadline_start(&RX_DEADLINE, TIMEOUT_MS) != NORMAL) {
                return RESOLVE_ERROR;
        }
        clock_gettime(CLOCK_MONOTONIC, &TIME_START);

        TX_FRAME_LENGTH = 0;
        STATUS = kwire_write(
                _prog_data,
                PCAP,
                &PACKET,
                &TX_FRAME_LENGTH
        );

        if (STATUS != NOSIX_OK) {
                kui_add_line_and_render(
                        NOTICE_ERROR
                        "DNS transmission to "
                        ANSI_COLOR_CYAN
                        "%s"
                        ANSI_COLOR_RESET
                        " failed: "
                        ANSI_COLOR_CYAN
                        "%s"
                        ANSI_COLOR_RESET
                        " (%d).",
                        SERVER,
                        kresolve_nosix_status_string(STATUS),
                        STATUS
                );
                return RESOLVE_ERROR;
        }

        kui_add_line_and_render(
                NOTICE_TRANSMISSION
                "DNS %s query for "
                ANSI_COLOR_CYAN
                "%s"
                ANSI_COLOR_RESET
                " sent to "
                ANSI_COLOR_CYAN
                "%s"
                ANSI_COLOR_RESET
                " via "
                ANSI_COLOR_CYAN
                "UDP/53"
                ANSI_COLOR_RESET
                " ("
                RGB_COLOR_BRIGHT_GREEN
                "%zu"
                ANSI_COLOR_RESET
                " bytes).",
                TYPE == DNS_A ? "A" : "AAAA",
                HOST,
                SERVER,
                TX_FRAME_LENGTH
        );

        for (;;) {
                memset(CATCH, 0x00, sizeof(CATCH));
                CAPTURE.frame.length = 0;
                CAPTURE.wire_length = 0;
                CAPTURE.timestamp_ns = 0;
                CAPTURE.interface_index = 0;
                CAPTURE.flags = 0;

                STATUS = kwire_read_until(
                        _prog_data, PCAP, &CAPTURE, &RX_DEADLINE
                );

                if (STATUS == NOSIX_TIMEOUT) {
                        return RESOLVE_TIMEOUT;
                }

                if (
                        STATUS != NOSIX_OK
                        && STATUS != NOSIX_TRUNCATED
                ) {
                        kui_add_line_and_render(
                                NOTICE_ERROR
                                "DNS capture failed: "
                                ANSI_COLOR_CYAN
                                "%s"
                                ANSI_COLOR_RESET
                                " (%d).",
                                kresolve_nosix_status_string(STATUS),
                                STATUS
                        );
                        return RESOLVE_ERROR;
                }

                DNS_PAYLOAD = NULL;
                DNS_PAYLOAD_LENGTH = 0;

                if (
                        capture_dns_payload_ipv4(
                                &CAPTURE,
                                SERVER_IPV4,
                                SOURCE_PORT,
                                &DNS_PAYLOAD,
                                &DNS_PAYLOAD_LENGTH
                        ) == NORMAL
                        && DNS_PAYLOAD_LENGTH >= 12
                        && read_u16(DNS_PAYLOAD) == ID
                ) {
                        int RESULT;

                        kwire_rx_accept(
                                _prog_data,
                                &CAPTURE
                        );

                        kui_add_line_and_render(
                                NOTICE_RECIEVE
                                "DNS response received from "
                                ANSI_COLOR_CYAN
                                "%s"
                                ANSI_COLOR_RESET
                                " ("
                                RGB_COLOR_BRIGHT_GREEN
                                "%zu"
                                ANSI_COLOR_RESET
                                " bytes).",
                                SERVER,
                                DNS_PAYLOAD_LENGTH
                        );

                        RESULT = parse_answer(
                                DNS_PAYLOAD,
                                DNS_PAYLOAD_LENGTH,
                                ID,
                                TYPE,
                                ADDRESS,
                                ADDRESS_SIZE
                        );

                        return RESULT;
                }

                clock_gettime(CLOCK_MONOTONIC, &TIME_NOW);
                if (
                        dns_elapsed_ms(
                                &TIME_START,
                                &TIME_NOW
                        ) >= (double)TIMEOUT_MS
                ) {
                        return RESOLVE_TIMEOUT;
                }
        }
}

static int profile_query(
        _carry_forward * _prog_data,
        KPCAP *PCAP,
        const char *HOST,
        int FAMILY,
        char *ADDRESS,
        size_t ADDRESS_SIZE
) {
        const char *SERVERS[2];
        uint16_t TYPE;
        int RETRIES;
        int STATUS;

        if (
                !_prog_data
                || !HOST
                || !ADDRESS
                || ADDRESS_SIZE == 0
        ) {
                return RESOLVE_ERROR;
        }

        ADDRESS[0] = 0x00;

        if (
                !PCAP
                || !_prog_data->nosix_net
        ) {
                return RESOLVE_ERROR;
        }

        TYPE = FAMILY == AF_INET ? DNS_A : DNS_AAAA;

        SERVERS[0] = (const char*)_prog_data->gprof.dns_server1;
        SERVERS[1] = (const char*)_prog_data->gprof.dns_server2;

        RETRIES = _prog_data->gprof.dns_retries < 0
                ? 0
                : _prog_data->gprof.dns_retries;

        STATUS = RESOLVE_ERROR;

        for (int SERVER_INDEX = 0; SERVER_INDEX < 2; SERVER_INDEX++) {
                if (
                        !SERVERS[SERVER_INDEX]
                        || SERVERS[SERVER_INDEX][0] == 0x00
                ) {
                        continue;
                }

                for (
                        int ATTEMPT = 0;
                        ATTEMPT <= RETRIES;
                        ATTEMPT++
                ) {
                        STATUS = dns_query(
                                _prog_data,
                                PCAP,
                                SERVERS[SERVER_INDEX],
                                HOST,
                                TYPE,
                                _prog_data->gprof.dns_timeout_ms,
                                ADDRESS,
                                ADDRESS_SIZE
                        );

                        if (
                                STATUS == RESOLVE_OK
                                || STATUS == RESOLVE_NOT_FOUND
                        ) {
                                break;
                        }
                }

                if (
                        STATUS == RESOLVE_OK
                        || STATUS == RESOLVE_NOT_FOUND
                ) {
                        break;
                }
        }

        return STATUS;
}

static int save_target(
        _carry_forward * _prog_data,
        const unsigned char *TID,
        const TARGET *PETAL
) {
        char PATH[MAX_PATH];

        memset(PATH, 0x00, sizeof(PATH));

        if (
                build_petal_path(
                        PATH,
                        sizeof(PATH),
                        _prog_data->wd,
                        _prog_data->active_project,
                        TID
                ) != NORMAL
        ) {
                return ABNORMAL;
        }

        return targets_write_petal_data(
                PATH,
                PETAL
        );
}

static void resolve_target(
        _carry_forward * _prog_data,
        const unsigned char *TID,
        TARGET *PETAL
) {
        char HOST[DOMAIN_BLOCK];
        char IPV4[IPV4_BLOCK];
        char IPV6[IPV6_BLOCK];
        int IPV4_STATUS;
        int IPV6_STATUS;
        int8_t CHANGED;
        int8_t PCAP_OPEN;
        struct in_addr IPV4_LITERAL;
        struct in6_addr IPV6_LITERAL;
        KPCAP PCAP;

        if (
                !_prog_data
                || !TID
                || !PETAL
        ) {
                return;
        }

        if (PETAL->URL[0] == 0x00) {
                kui_add_line_and_render(
                        NOTICE_NOPE
                        "Target %s does not have a URL.",
                        TID
                );
                return;
        }

        memset(HOST, 0x00, sizeof(HOST));
        memset(IPV4, 0x00, sizeof(IPV4));
        memset(IPV6, 0x00, sizeof(IPV6));
        memset(&PCAP, 0x00, sizeof(PCAP));

        CHANGED = ISFALSE;
        PCAP_OPEN = ISFALSE;

        if (
                hostname_from_url(
                        PETAL->URL,
                        HOST,
                        sizeof(HOST)
                ) != NORMAL
        ) {
                kui_add_line_and_render(
                        NOTICE_NOPE
                        "Unable to extract a hostname from %s.",
                        PETAL->URL
                );
                return;
        }

        if (
                inet_pton(
                        AF_INET,
                        HOST,
                        &IPV4_LITERAL
                ) == 1
        ) {
                snprintf(
                        IPV4,
                        sizeof(IPV4),
                        "%s",
                        HOST
                );
                IPV4_STATUS = RESOLVE_OK;
                IPV6_STATUS = RESOLVE_NOT_FOUND;
        } else if (
                inet_pton(
                        AF_INET6,
                        HOST,
                        &IPV6_LITERAL
                ) == 1
        ) {
                snprintf(
                        IPV6,
                        sizeof(IPV6),
                        "%s",
                        HOST
                );
                IPV4_STATUS = RESOLVE_NOT_FOUND;
                IPV6_STATUS = RESOLVE_OK;
        } else {
                if (
                        apply_search_domain(
                                &_prog_data->gprof,
                                HOST,
                                sizeof(HOST)
                        ) != NORMAL
                ) {
                        kui_add_line_and_render(
                                NOTICE_ERROR
                                "DNS search-domain expansion failed for target %s.",
                                TID
                        );
                        return;
                }

                if (!_prog_data->nosix_net) {
                        kui_add_line_and_render(
                                NOTICE_ERROR
                                "NOSIX network runtime is offline."
                        );
                        return;
                }

                uint64_t SCAN_TIMESTAMP_NS = kscan_now_ns();

                if (
                        kwire_pcap_open(
                                _prog_data,
                                &PCAP,
                                "DNS",
                                TID,
                                SCAN_TIMESTAMP_NS
                        ) == NORMAL
                ) {
                        PCAP_OPEN = ISTRUE;
                } else {
                        kui_add_line_and_render(
                                NOTICE_ERROR
                                "Failed to open DNS packet capture."
                        );
                        return;
                }

                IPV4_STATUS = profile_query(
                        _prog_data,
                        PCAP_OPEN == ISTRUE ? &PCAP : NULL,
                        HOST,
                        AF_INET,
                        IPV4,
                        sizeof(IPV4)
                );

                IPV6_STATUS = profile_query(
                        _prog_data,
                        PCAP_OPEN == ISTRUE ? &PCAP : NULL,
                        HOST,
                        AF_INET6,
                        IPV6,
                        sizeof(IPV6)
                );

                if (PCAP_OPEN == ISTRUE) {
                        kwire_pcap_close(&PCAP);
                }
        }

        if (
                IPV4_STATUS == RESOLVE_OK
                && IPV4[0]
        ) {
                if (
                        strcmp(
                                (char*)PETAL->IPV4,
                                IPV4
                        ) != MATCH
                ) {
                        memset(
                                PETAL->IPV4,
                                0x00,
                                sizeof(PETAL->IPV4)
                        );

                        snprintf(
                                (char*)PETAL->IPV4,
                                sizeof(PETAL->IPV4),
                                "%s",
                                IPV4
                        );

                        CHANGED = ISTRUE;
                }

                kui_add_line_and_render(
                        NOTICE_SUCCESS
                        ANSI_COLOR_CYAN
                        "%s"
                        ANSI_COLOR_RESET
                        " -> IPv4 "
                        ANSI_COLOR_CYAN
                        "%s"
                        ANSI_COLOR_RESET,
                        HOST,
                        IPV4
                );
        }

        if (
                IPV6_STATUS == RESOLVE_OK
                && IPV6[0]
        ) {
                if (
                        strcmp(
                                (char*)PETAL->IPV6,
                                IPV6
                        ) != MATCH
                ) {
                        memset(
                                PETAL->IPV6,
                                0x00,
                                sizeof(PETAL->IPV6)
                        );

                        snprintf(
                                (char*)PETAL->IPV6,
                                sizeof(PETAL->IPV6),
                                "%s",
                                IPV6
                        );

                        CHANGED = ISTRUE;
                }

                kui_add_line_and_render(
                        NOTICE_SUCCESS
                        ANSI_COLOR_CYAN
                        "%s"
                        ANSI_COLOR_RESET
                        " -> IPv6 "
                        ANSI_COLOR_CYAN
                        "%s"
                        ANSI_COLOR_RESET,
                        HOST,
                        IPV6
                );
        }

        if (
                IPV4_STATUS != RESOLVE_OK
                && IPV6_STATUS != RESOLVE_OK
        ) {
                if (
                        IPV4_STATUS == RESOLVE_NOT_FOUND
                        && IPV6_STATUS == RESOLVE_NOT_FOUND
                ) {
                        kui_add_line_and_render(
                                NOTICE_NOPE
                                "No A or AAAA record resolved for "
                                ANSI_COLOR_CYAN
                                "%s"
                                ANSI_COLOR_RESET
                                ".",
                                HOST
                        );
                } else {
                        kui_add_line_and_render(
                                NOTICE_ERROR
                                "DNS resolution failed for "
                                ANSI_COLOR_CYAN
                                "%s"
                                ANSI_COLOR_RESET
                                ".",
                                HOST
                        );
                }

                return;
        }

        if (
                CHANGED == ISTRUE
                && save_target(
                        _prog_data,
                        TID,
                        PETAL
                ) != NORMAL
        ) {
                kui_add_line_and_render(
                        NOTICE_ERROR
                        "Failed to save resolved addresses for target %s.",
                        TID
                );
        }
}

void kresolve_single(_carry_forward * _prog_data) {
        if (
                !_prog_data
                || !_prog_data->active_project_active_target
                || !_prog_data->active_project_active_target->PETAL
        ) {
                return;
        }

        if (_prog_data->cmd_tokens_count != 1) {
                kui_add_line("< Usage: resolve");
                return;
        }

        resolve_target(
                _prog_data,
                _prog_data->active_project_active_target->TID,
                _prog_data->active_project_active_target->PETAL
        );
}

void kresolve_multi(_carry_forward * _prog_data) {
        FLOWER *NODE;

        if (
                !_prog_data
                || !_prog_data->active_project_flower
        ) {
                return;
        }

        NODE = _prog_data->active_project_flower->NEXT;

        while (NODE) {
                TARGET PETAL;
                char PATH[MAX_PATH];

                memset(&PETAL, 0x00, sizeof(PETAL));
                memset(PATH, 0x00, sizeof(PATH));

                if (
                        build_petal_path(
                                PATH,
                                sizeof(PATH),
                                _prog_data->wd,
                                _prog_data->active_project,
                                NODE->TID
                        ) != NORMAL
                ) {
                        kui_add_line_and_render(
                                NOTICE_ERROR
                                "Failed to build target path for %s.",
                                NODE->TID
                        );

                        NODE = NODE->NEXT;
                        continue;
                }

                if (
                        targets_read_petal_data(
                                PATH,
                                &PETAL
                        ) != NORMAL
                ) {
                        kui_add_line_and_render(
                                NOTICE_ERROR
                                "Failed to read target %s.",
                                NODE->TID
                        );

                        NODE = NODE->NEXT;
                        continue;
                }

                kui_add_line_and_render(
                        NOTICE_INFO
                        "Resolving target "
                        ANSI_COLOR_CYAN
                        "%s"
                        ANSI_COLOR_RESET
                        ".",
                        NODE->TID
                );

                resolve_target(
                        _prog_data,
                        NODE->TID,
                        &PETAL
                );

                NODE = NODE->NEXT;
        }
}
