// Copyright 2026 Jamison A. Drapeau
#define _POSIX_C_SOURCE 200809L

#include "kbanner.h"
#include "kportscan_internal.h"
#include "kscan.h"
#include "kui.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#define KBANNER_STORE_VERSION          1U
#define KBANNER_BITMAP_BYTES  (MAX_PORTS / 8U)
#define KBANNER_MAX_BYTES           4096U
#define KBANNER_DISPLAY_BYTES        128U
#define KBANNER_ASCII_BYTES          192U
#define KBANNER_CONNECT_TIMEOUT_MS  1000

#define KBANNER_SERVICE_BLOCK          32U
#define KBANNER_MODE_BLOCK             16U
#define KBANNER_PROBE_BLOCK            32U

typedef enum KBANNER_PROBE_KIND {
        KBANNER_PROBE_NONE = 0,
        KBANNER_PROBE_HTTP_GET,
        KBANNER_PROBE_TLS_CLIENT_HELLO
} KBANNER_PROBE_KIND;

typedef struct KBANNER_EVIDENCE {
        uint64_t TIMESTAMP_NS;
        uint16_t PORT;
        uint8_t FAMILY;
        char SERVICE[KBANNER_SERVICE_BLOCK];
        char MODE[KBANNER_MODE_BLOCK];
        char PROBE[KBANNER_PROBE_BLOCK];
        size_t BANNER_BYTES;
        uint8_t BANNER[KBANNER_MAX_BYTES];
        int8_t PRESENT;
} KBANNER_EVIDENCE;

static int kbanner_path(
        char * OUT,
        size_t OUT_SIZE,
        const _carry_forward * _prog_data,
        const unsigned char * TID,
        const char * NAME
) {
        int WRITTEN;

        if (!OUT || OUT_SIZE == 0 || !_prog_data || !TID || !NAME) {
                return ABNORMAL;
        }

        WRITTEN = snprintf(
                OUT,
                OUT_SIZE,
                "%s/%s%s/%s/%s",
                _prog_data->wd,
                KAMI_USER_PROJECTS_DIR,
                _prog_data->active_project,
                TID,
                NAME
        );

        return (
                WRITTEN >= 0
                && WRITTEN < (int)OUT_SIZE
        ) ? NORMAL : ABNORMAL;
}

static void kbanner_bitmap_set(uint8_t * BITMAP, uint16_t PORT) {
        if (!BITMAP || PORT == 0) {
                return;
        }

        BITMAP[(size_t)PORT >> 3] |= (uint8_t)(1U << (PORT & 7U));
}

static int8_t kbanner_bitmap_has(
        const uint8_t * BITMAP,
        uint16_t PORT
) {
        if (!BITMAP || PORT == 0) {
                return ISFALSE;
        }

        return (
                BITMAP[(size_t)PORT >> 3]
                & (uint8_t)(1U << (PORT & 7U))
        ) ? ISTRUE : ISFALSE;
}

static uint64_t kbanner_load_open_ports(
        _carry_forward * _prog_data,
        const unsigned char * TID,
        nosix_address_family_t FAMILY,
        uint64_t SCAN_START_NS,
        uint8_t * OPEN_PORTS
) {
        char PATH[MAX_PATH];
        char LINE[512];
        FILE * FILE_HANDLE;
        uint64_t TOTAL = 0;

        if (
                !_prog_data
                || !TID
                || !OPEN_PORTS
                || SCAN_START_NS == 0
        ) {
                return 0;
        }

        memset(PATH, 0x00, sizeof(PATH));
        if (
                kbanner_path(
                        PATH,
                        sizeof(PATH),
                        _prog_data,
                        TID,
                        ".ports"
                ) != NORMAL
        ) {
                return 0;
        }

        FILE_HANDLE = fopen(PATH, "r");
        if (!FILE_HANDLE) {
                return 0;
        }

        while (fgets(LINE, sizeof(LINE), FILE_HANDLE)) {
                unsigned long long TIMESTAMP_NS = 0;
                char PROTOCOL[8];
                unsigned int AF = 0;
                unsigned int PORT = 0;
                char STATE[32];
                char EVIDENCE[64];
                unsigned int RX_BYTES = 0;

                if (LINE[0] == '#') {
                        continue;
                }

                memset(PROTOCOL, 0x00, sizeof(PROTOCOL));
                memset(STATE, 0x00, sizeof(STATE));
                memset(EVIDENCE, 0x00, sizeof(EVIDENCE));

                if (
                        sscanf(
                                LINE,
                                "%llu\t%7s\t%u\t%u\t%31s\t%63s\t%u",
                                &TIMESTAMP_NS,
                                PROTOCOL,
                                &AF,
                                &PORT,
                                STATE,
                                EVIDENCE,
                                &RX_BYTES
                        ) != 7
                ) {
                        continue;
                }

                if (
                        (uint64_t)TIMESTAMP_NS < SCAN_START_NS
                        || strcmp(PROTOCOL, "TCP") != MATCH
                        || AF != (unsigned int)FAMILY
                        || PORT == 0
                        || PORT >= MAX_PORTS
                        || strcmp(STATE, "OPEN") != MATCH
                ) {
                        continue;
                }

                if (
                        kbanner_bitmap_has(
                                OPEN_PORTS,
                                (uint16_t)PORT
                        ) != ISTRUE
                ) {
                        kbanner_bitmap_set(
                                OPEN_PORTS,
                                (uint16_t)PORT
                        );
                        TOTAL++;
                }
        }

        fclose(FILE_HANDLE);
        return TOTAL;
}

static int32_t kbanner_connect_timeout(
        const _carry_forward * _prog_data
) {
        int32_t TIMEOUT;

        if (!_prog_data) {
                return KBANNER_CONNECT_TIMEOUT_MS;
        }

        TIMEOUT = _prog_data->gprof.rx_timeout_ms;
        if (TIMEOUT <= 0) {
                return KBANNER_CONNECT_TIMEOUT_MS;
        }
        if (TIMEOUT < 250) {
                return 250;
        }
        if (TIMEOUT > 1500) {
                return 1500;
        }
        return TIMEOUT;
}

static int kbanner_destination(
        nosix_address_t * DESTINATION,
        const char * ADDRESS,
        nosix_address_family_t FAMILY
) {
        int AF;

        if (!DESTINATION || !ADDRESS) {
                return ABNORMAL;
        }

        memset(DESTINATION, 0x00, sizeof(*DESTINATION));
        DESTINATION->family = FAMILY;
        AF = FAMILY == NOSIX_ADDRESS_IPV4 ? AF_INET : AF_INET6;

        if (
                inet_pton(
                        AF,
                        ADDRESS,
                        FAMILY == NOSIX_ADDRESS_IPV4
                                ? (void *)DESTINATION->bytes.ipv4
                                : (void *)DESTINATION->bytes.ipv6
                ) != 1
        ) {
                return ABNORMAL;
        }

        return NORMAL;
}

static int8_t kbanner_http_port(uint16_t PORT) {
        switch (PORT) {
                case 80:
                case 81:
                case 3000:
                case 5000:
                case 5985:
                case 8000:
                case 8008:
                case 8080:
                case 8081:
                case 8888:
                case 9000:
                case 9200:
                        return ISTRUE;
                default:
                        return ISFALSE;
        }
}

static int8_t kbanner_tls_port(uint16_t PORT) {
        switch (PORT) {
                case 443:
                case 465:
                case 636:
                case 853:
                case 990:
                case 993:
                case 995:
                case 5061:
                case 5986:
                case 8443:
                case 9443:
                        return ISTRUE;
                default:
                        return ISFALSE;
        }
}

static size_t kbanner_http_probe(
        uint8_t * OUT,
        size_t OUT_SIZE,
        const char * ADDRESS,
        nosix_address_family_t FAMILY
) {
        char HOST[IP_BLOCK + 4];
        int WRITTEN;

        if (!OUT || OUT_SIZE == 0 || !ADDRESS) {
                return 0;
        }

        memset(HOST, 0x00, sizeof(HOST));

        if (FAMILY == NOSIX_ADDRESS_IPV6) {
                if (
                        snprintf(
                                HOST,
                                sizeof(HOST),
                                "[%s]",
                                ADDRESS
                        ) >= (int)sizeof(HOST)
                ) {
                        return 0;
                }
        } else {
                if (
                        snprintf(
                                HOST,
                                sizeof(HOST),
                                "%s",
                                ADDRESS
                        ) >= (int)sizeof(HOST)
                ) {
                        return 0;
                }
        }

        WRITTEN = snprintf(
                (char *)OUT,
                OUT_SIZE,
                "GET / HTTP/1.0\r\n"
                "Host: %s\r\n"
                "User-Agent: Kaminowaku/%s\r\n"
                "Accept: */*\r\n"
                "Connection: close\r\n"
                "\r\n",
                HOST,
                VERSION
        );

        return (
                WRITTEN > 0
                && WRITTEN < (int)OUT_SIZE
        ) ? (size_t)WRITTEN : 0;
}

static void kbanner_u16(
        uint8_t * BUFFER,
        size_t OFFSET,
        uint16_t VALUE
) {
        BUFFER[OFFSET] = (uint8_t)(VALUE >> 8);
        BUFFER[OFFSET + 1] = (uint8_t)VALUE;
}

static size_t kbanner_tls_probe(
        uint8_t * OUT,
        size_t OUT_SIZE
) {
        size_t POS = 0;
        size_t HANDSHAKE_LENGTH_OFFSET;
        size_t EXTENSION_LENGTH_OFFSET;
        size_t EXTENSION_START;
        uint64_t SEED;
        uint32_t HANDSHAKE_LENGTH;
        uint16_t RECORD_LENGTH;
        uint16_t EXTENSION_LENGTH;

        if (!OUT || OUT_SIZE < 128) {
                return 0;
        }

        memset(OUT, 0x00, OUT_SIZE);

        OUT[POS++] = 0x16;
        OUT[POS++] = 0x03;
        OUT[POS++] = 0x01;
        POS += 2;

        OUT[POS++] = 0x01;
        HANDSHAKE_LENGTH_OFFSET = POS;
        POS += 3;

        OUT[POS++] = 0x03;
        OUT[POS++] = 0x03;

        SEED = kscan_now_ns();
        for (size_t INDEX = 0; INDEX < 32; INDEX++) {
                SEED ^= SEED << 13;
                SEED ^= SEED >> 7;
                SEED ^= SEED << 17;
                OUT[POS++] = (uint8_t)(SEED >> ((INDEX & 7U) * 8U));
        }

        OUT[POS++] = 0x00;

        kbanner_u16(OUT, POS, 10);
        POS += 2;
        OUT[POS++] = 0x13;
        OUT[POS++] = 0x01;
        OUT[POS++] = 0x13;
        OUT[POS++] = 0x02;
        OUT[POS++] = 0xc0;
        OUT[POS++] = 0x2f;
        OUT[POS++] = 0xc0;
        OUT[POS++] = 0x30;
        OUT[POS++] = 0x00;
        OUT[POS++] = 0x2f;

        OUT[POS++] = 0x01;
        OUT[POS++] = 0x00;

        EXTENSION_LENGTH_OFFSET = POS;
        POS += 2;
        EXTENSION_START = POS;

        kbanner_u16(OUT, POS, 0x002b);
        POS += 2;
        kbanner_u16(OUT, POS, 5);
        POS += 2;
        OUT[POS++] = 0x04;
        OUT[POS++] = 0x03;
        OUT[POS++] = 0x04;
        OUT[POS++] = 0x03;
        OUT[POS++] = 0x03;

        kbanner_u16(OUT, POS, 0x000a);
        POS += 2;
        kbanner_u16(OUT, POS, 6);
        POS += 2;
        kbanner_u16(OUT, POS, 4);
        POS += 2;
        kbanner_u16(OUT, POS, 0x001d);
        POS += 2;
        kbanner_u16(OUT, POS, 0x0017);
        POS += 2;

        kbanner_u16(OUT, POS, 0x000d);
        POS += 2;
        kbanner_u16(OUT, POS, 10);
        POS += 2;
        kbanner_u16(OUT, POS, 8);
        POS += 2;
        kbanner_u16(OUT, POS, 0x0403);
        POS += 2;
        kbanner_u16(OUT, POS, 0x0804);
        POS += 2;
        kbanner_u16(OUT, POS, 0x0805);
        POS += 2;
        kbanner_u16(OUT, POS, 0x0806);
        POS += 2;

        kbanner_u16(OUT, POS, 0x000b);
        POS += 2;
        kbanner_u16(OUT, POS, 2);
        POS += 2;
        OUT[POS++] = 0x01;
        OUT[POS++] = 0x00;

        if (POS > OUT_SIZE) {
                return 0;
        }

        EXTENSION_LENGTH = (uint16_t)(POS - EXTENSION_START);
        kbanner_u16(
                OUT,
                EXTENSION_LENGTH_OFFSET,
                EXTENSION_LENGTH
        );

        HANDSHAKE_LENGTH = (uint32_t)(POS - 9);
        OUT[HANDSHAKE_LENGTH_OFFSET] = (uint8_t)(HANDSHAKE_LENGTH >> 16);
        OUT[HANDSHAKE_LENGTH_OFFSET + 1] = (uint8_t)(HANDSHAKE_LENGTH >> 8);
        OUT[HANDSHAKE_LENGTH_OFFSET + 2] = (uint8_t)HANDSHAKE_LENGTH;

        RECORD_LENGTH = (uint16_t)(POS - 5);
        kbanner_u16(OUT, 3, RECORD_LENGTH);

        return POS;
}

static nosix_status_t kbanner_collect(
        nosix_stream_t * STREAM,
        uint8_t * OUT,
        size_t OUT_SIZE,
        size_t * OUT_LENGTH,
        int32_t TIMEOUT_MS
) {
        size_t TOTAL = 0;
        KWIRE_DEADLINE DEADLINE;

        if (OUT_LENGTH) {
                *OUT_LENGTH = 0;
        }

        if (!STREAM || !OUT || OUT_SIZE == 0) {
                return NOSIX_ERR_ARGUMENT;
        }

        if (kwire_deadline_start(&DEADLINE, TIMEOUT_MS) != NORMAL) {
                return NOSIX_ERR_SYSTEM;
        }

        while (TOTAL < OUT_SIZE) {
                int32_t REMAINING = kwire_deadline_remaining_ms(&DEADLINE);
                size_t RECEIVED = 0;
                if (REMAINING <= 0) {
                        if (OUT_LENGTH) *OUT_LENGTH = TOTAL;
                        return TOTAL > 0 ? NOSIX_OK
                                : REMAINING == 0 ? NOSIX_TIMEOUT : NOSIX_ERR_SYSTEM;
                }
                nosix_status_t STATUS = nosix_stream_read(
                        STREAM,
                        OUT + TOTAL,
                        OUT_SIZE - TOTAL,
                        &RECEIVED,
                        REMAINING
                );

                if (STATUS == NOSIX_OK) {
                        if (RECEIVED == 0) {
                                continue;
                        }

                        kui_add_line_and_render(
                                NOTICE_RECIEVE
                                "Banner stream received "
                                RGB_COLOR_BRIGHT_GREEN
                                "%zu"
                                ANSI_COLOR_RESET
                                " bytes.",
                                RECEIVED
                        );

                        TOTAL += RECEIVED;
                        continue;
                }

                if (STATUS == NOSIX_TIMEOUT || STATUS == NOSIX_EOF) {
                        if (OUT_LENGTH) {
                                *OUT_LENGTH = TOTAL;
                        }
                        return TOTAL > 0 ? NOSIX_OK : STATUS;
                }

                if (OUT_LENGTH) {
                        *OUT_LENGTH = TOTAL;
                }
                return STATUS;
        }

        if (OUT_LENGTH) {
                *OUT_LENGTH = TOTAL;
        }
        return NOSIX_OK;
}

static int kbanner_printable_ratio(
        const uint8_t * DATA,
        size_t LENGTH
) {
        size_t PRINTABLE = 0;

        if (!DATA || LENGTH == 0) {
                return 0;
        }

        for (size_t INDEX = 0; INDEX < LENGTH; INDEX++) {
                if (
                        isprint((unsigned char)DATA[INDEX])
                        || DATA[INDEX] == '\r'
                        || DATA[INDEX] == '\n'
                        || DATA[INDEX] == '\t'
                ) {
                        PRINTABLE++;
                }
        }

        return (int)((PRINTABLE * 100U) / LENGTH);
}

static const char * kbanner_classify(
        const uint8_t * DATA,
        size_t LENGTH,
        uint16_t PORT,
        KBANNER_PROBE_KIND PROBE
) {
        if (DATA && LENGTH >= 4 && memcmp(DATA, "SSH-", 4) == MATCH) {
                return "SSH";
        }

        if (DATA && LENGTH >= 5 && memcmp(DATA, "HTTP/", 5) == MATCH) {
                return "HTTP";
        }

        if (
                DATA
                && LENGTH >= 3
                && DATA[0] >= 0x14
                && DATA[0] <= 0x17
                && DATA[1] == 0x03
        ) {
                return "TLS";
        }

        if (DATA && LENGTH >= 4 && memcmp(DATA, "RFB ", 4) == MATCH) {
                return "VNC";
        }

        if (DATA && LENGTH >= 3 && memcmp(DATA, "+OK", 3) == MATCH) {
                return PORT == 110 ? "POP3" : "TEXT";
        }

        if (DATA && LENGTH >= 4 && memcmp(DATA, "* OK", 4) == MATCH) {
                return PORT == 143 ? "IMAP" : "TEXT";
        }

        if (
                DATA
                && LENGTH >= 3
                && DATA[0] == '2'
                && DATA[1] == '2'
                && DATA[2] == '0'
        ) {
                if (PORT == 21 || PORT == 990) {
                        return "FTP";
                }
                if (PORT == 25 || PORT == 465 || PORT == 587) {
                        return "SMTP";
                }
                return "TEXT";
        }

        if (PROBE == KBANNER_PROBE_HTTP_GET && LENGTH > 0) {
                return "HTTP?";
        }

        if (PROBE == KBANNER_PROBE_TLS_CLIENT_HELLO && LENGTH > 0) {
                return "TLS?";
        }

        if (kbanner_printable_ratio(DATA, LENGTH) >= 70) {
                return "TEXT";
        }

        return "UNKNOWN";
}

static const char * kbanner_probe_string(KBANNER_PROBE_KIND PROBE) {
        switch (PROBE) {
                case KBANNER_PROBE_HTTP_GET: return "HTTP_GET";
                case KBANNER_PROBE_TLS_CLIENT_HELLO: return "TLS_CLIENT_HELLO";
                default: return "NONE";
        }
}

static int kbanner_store(
        _carry_forward * _prog_data,
        const unsigned char * TID,
        nosix_address_family_t FAMILY,
        uint16_t PORT,
        const char * SERVICE,
        const char * MODE,
        KBANNER_PROBE_KIND PROBE,
        const uint8_t * DATA,
        size_t LENGTH
) {
        char PATH[MAX_PATH];
        FILE * FILE_HANDLE;
        long SIZE;
        uint64_t TIMESTAMP_NS;

        if (
                !_prog_data
                || !TID
                || PORT == 0
                || !SERVICE
                || !MODE
                || (LENGTH > 0 && !DATA)
        ) {
                return ABNORMAL;
        }

        if (LENGTH > KBANNER_MAX_BYTES) {
                LENGTH = KBANNER_MAX_BYTES;
        }

        memset(PATH, 0x00, sizeof(PATH));
        if (
                kbanner_path(
                        PATH,
                        sizeof(PATH),
                        _prog_data,
                        TID,
                        ".services"
                ) != NORMAL
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
                        "# KAMINOWAKU SERVICES v%u\n"
                        "# timestamp_ns\tprotocol\taf\tport\tservice\tmode\tprobe\tbanner_bytes\tbanner_hex\n",
                        KBANNER_STORE_VERSION
                );
        }

        TIMESTAMP_NS = kscan_now_ns();

        if (
                fprintf(
                        FILE_HANDLE,
                        "%llu\tTCP\t%u\t%u\t%s\t%s\t%s\t%zu\t",
                        (unsigned long long)TIMESTAMP_NS,
                        (unsigned int)FAMILY,
                        PORT,
                        SERVICE,
                        MODE,
                        kbanner_probe_string(PROBE),
                        LENGTH
                ) < 0
        ) {
                fclose(FILE_HANDLE);
                return ABNORMAL;
        }

        if (LENGTH == 0) {
                if (fputc('-', FILE_HANDLE) == EOF) {
                        fclose(FILE_HANDLE);
                        return ABNORMAL;
                }
        } else {
                for (size_t INDEX = 0; INDEX < LENGTH; INDEX++) {
                        if (fprintf(FILE_HANDLE, "%02X", DATA[INDEX]) < 0) {
                                fclose(FILE_HANDLE);
                                return ABNORMAL;
                        }
                }
        }

        if (fputc('\n', FILE_HANDLE) == EOF) {
                fclose(FILE_HANDLE);
                return ABNORMAL;
        }

        fflush(FILE_HANDLE);
        fclose(FILE_HANDLE);
        return NORMAL;
}

static void kbanner_emit(
        uint16_t PORT,
        const char * SERVICE,
        const char * MODE,
        KBANNER_PROBE_KIND PROBE,
        size_t LENGTH
) {
        if (!SERVICE || !MODE) {
                return;
        }

        kui_add_line_and_render(
                NOTICE_SUCCESS
                "TCP/%u " ANSI_COLOR_CYAN "BANNER" ANSI_COLOR_RESET
                " -> " RGB_COLOR_BRIGHT_GREEN "%s" ANSI_COLOR_RESET
                " (%zu bytes, %s%s%s)",
                PORT,
                SERVICE,
                LENGTH,
                MODE,
                PROBE == KBANNER_PROBE_NONE ? "" : ", ",
                PROBE == KBANNER_PROBE_NONE
                        ? ""
                        : kbanner_probe_string(PROBE)
        );
}

static void kbanner_capture_flush(_carry_forward * _prog_data) {
        uint8_t CATCH[OUT_BLOCK];
        nosix_capture_t CAPTURE;

        if (!_prog_data || !_prog_data->nosix_net) {
                return;
        }

        memset(CATCH, 0x00, sizeof(CATCH));
        memset(&CAPTURE, 0x00, sizeof(CAPTURE));
        CAPTURE.frame.data = CATCH;
        CAPTURE.frame.capacity = sizeof(CATCH);

        KWIRE_DEADLINE DEADLINE;
        if (kwire_deadline_start(&DEADLINE, 1000) != NORMAL) return;

        for (unsigned int INDEX = 0; INDEX < 4096U; INDEX++) {
                nosix_status_t STATUS;
                int32_t REMAINING = kwire_deadline_remaining_ms(&DEADLINE);
                if (REMAINING <= 0) break;

                CAPTURE.frame.length = 0;
                CAPTURE.wire_length = 0;
                CAPTURE.timestamp_ns = 0;
                CAPTURE.interface_index = 0;
                CAPTURE.flags = 0;

                STATUS = nosix_read_timeout(
                        _prog_data->nosix_net,
                        &CAPTURE,
                        1
                );

                if (STATUS == NOSIX_TIMEOUT) {
                        break;
                }

                if (STATUS != NOSIX_OK && STATUS != NOSIX_TRUNCATED) {
                        break;
                }

        }
}

static int kbanner_find_port_pcap(
        char * OUT,
        size_t OUT_SIZE,
        const _carry_forward * _prog_data,
        const unsigned char * TID,
        nosix_address_family_t FAMILY,
        uint16_t PORT,
        uint64_t SCAN_START_NS
) {
        char DIRECTORY[MAX_PATH];
        char PREFIX[128];
        size_t PREFIX_LENGTH;
        uint64_t BEST_TIMESTAMP = 0;
        DIR * HANDLE;
        struct dirent * ENTRY;

        if (
                !OUT
                || OUT_SIZE == 0
                || !_prog_data
                || !TID
                || PORT == 0
                || SCAN_START_NS == 0
        ) {
                return ABNORMAL;
        }

        memset(OUT, 0x00, OUT_SIZE);
        memset(DIRECTORY, 0x00, sizeof(DIRECTORY));
        memset(PREFIX, 0x00, sizeof(PREFIX));

        if (
                snprintf(
                        DIRECTORY,
                        sizeof(DIRECTORY),
                        "%s/%s%s/%s",
                        _prog_data->wd,
                        KAMI_USER_PROJECTS_DIR,
                        _prog_data->active_project,
                        TID
                ) >= (int)sizeof(DIRECTORY)
        ) {
                return ABNORMAL;
        }

        if (
                snprintf(
                        PREFIX,
                        sizeof(PREFIX),
                        "TCPv%c-%u-%s-",
                        FAMILY == NOSIX_ADDRESS_IPV4 ? '4' : '6',
                        PORT,
                        TID
                ) >= (int)sizeof(PREFIX)
        ) {
                return ABNORMAL;
        }

        PREFIX_LENGTH = strlen(PREFIX);
        HANDLE = opendir(DIRECTORY);
        if (!HANDLE) {
                return ABNORMAL;
        }

        while ((ENTRY = readdir(HANDLE)) != NULL) {
                const char * NAME = ENTRY->d_name;
                const char * TIMESTAMP_TEXT;
                char * END = NULL;
                unsigned long long TIMESTAMP;
                size_t NAME_LENGTH;

                if (strncmp(NAME, PREFIX, PREFIX_LENGTH) != MATCH) {
                        continue;
                }

                NAME_LENGTH = strlen(NAME);
                if (
                        NAME_LENGTH <= PREFIX_LENGTH + 5U
                        || strcmp(NAME + NAME_LENGTH - 5U, ".pcap") != MATCH
                ) {
                        continue;
                }

                TIMESTAMP_TEXT = NAME + PREFIX_LENGTH;
                TIMESTAMP = strtoull(TIMESTAMP_TEXT, &END, 10);
                if (
                        !END
                        || strcmp(END, ".pcap") != MATCH
                        || TIMESTAMP < SCAN_START_NS
                        || TIMESTAMP <= BEST_TIMESTAMP
                ) {
                        continue;
                }

                if (
                        snprintf(
                                OUT,
                                OUT_SIZE,
                                "%s/%s",
                                DIRECTORY,
                                NAME
                        ) >= (int)OUT_SIZE
                ) {
                        OUT[0] = 0x00;
                        continue;
                }

                BEST_TIMESTAMP = (uint64_t)TIMESTAMP;
        }

        closedir(HANDLE);
        return BEST_TIMESTAMP != 0 ? NORMAL : ABNORMAL;
}

static int8_t kbanner_capture_matches_stream(
        const nosix_capture_t * CAPTURE,
        const char * ADDRESS,
        nosix_address_family_t FAMILY,
        uint16_t REMOTE_PORT,
        uint16_t * LOCAL_PORT,
        int8_t * DIRECTION
) {
        const uint8_t * IP = NULL;
        size_t IP_LENGTH = 0;
        const uint8_t * TCP;
        uint16_t SOURCE_PORT;
        uint16_t DESTINATION_PORT;
        uint16_t CANDIDATE_LOCAL = 0;
        int8_t REMOTE_SOURCE = ISFALSE;
        int8_t REMOTE_DESTINATION = ISFALSE;

        if (DIRECTION) *DIRECTION = 0;

        if (
                !CAPTURE
                || !ADDRESS
                || REMOTE_PORT == 0
                || !LOCAL_PORT
                || kportscan_capture_l3(
                        CAPTURE,
                        FAMILY,
                        &IP,
                        &IP_LENGTH
                ) != NORMAL
        ) {
                return ISFALSE;
        }

        if (FAMILY == NOSIX_ADDRESS_IPV4) {
                size_t IHL;

                if (
                        IP_LENGTH < 20U
                        || (IP[0] >> 4) != 4U
                        || IP[9] != NOSIX_IPPROTO_TCP
                ) {
                        return ISFALSE;
                }

                IHL = (size_t)(IP[0] & 0x0fU) * 4U;
                if (IHL < 20U || IP_LENGTH < IHL + 20U) {
                        return ISFALSE;
                }

                REMOTE_SOURCE = kportscan_address_matches(
                        IP + 12,
                        ADDRESS,
                        FAMILY
                );
                REMOTE_DESTINATION = kportscan_address_matches(
                        IP + 16,
                        ADDRESS,
                        FAMILY
                );
                TCP = IP + IHL;
        } else {
                if (
                        IP_LENGTH < 60U
                        || (IP[0] >> 4) != 6U
                        || IP[6] != NOSIX_IPPROTO_TCP
                ) {
                        return ISFALSE;
                }

                REMOTE_SOURCE = kportscan_address_matches(
                        IP + 8,
                        ADDRESS,
                        FAMILY
                );
                REMOTE_DESTINATION = kportscan_address_matches(
                        IP + 24,
                        ADDRESS,
                        FAMILY
                );
                TCP = IP + 40;
        }

        SOURCE_PORT = (uint16_t)(((uint16_t)TCP[0] << 8) | TCP[1]);
        DESTINATION_PORT = (uint16_t)(((uint16_t)TCP[2] << 8) | TCP[3]);

        if (REMOTE_SOURCE == ISTRUE && SOURCE_PORT == REMOTE_PORT) {
                CANDIDATE_LOCAL = DESTINATION_PORT;
        } else if (
                REMOTE_DESTINATION == ISTRUE
                && DESTINATION_PORT == REMOTE_PORT
        ) {
                CANDIDATE_LOCAL = SOURCE_PORT;
        } else {
                return ISFALSE;
        }

        if (CANDIDATE_LOCAL == 0) {
                return ISFALSE;
        }

        if (*LOCAL_PORT == 0) {
                *LOCAL_PORT = CANDIDATE_LOCAL;
        }

        if (*LOCAL_PORT != CANDIDATE_LOCAL) {
                return ISFALSE;
        }

        if (DIRECTION) {
                *DIRECTION = REMOTE_SOURCE == ISTRUE ? -1 : 1;
        }

        return ISTRUE;
}

static int8_t kbanner_capture_matches_supporting(
        const nosix_capture_t * CAPTURE,
        const char * ADDRESS,
        nosix_address_family_t FAMILY,
        int8_t * DIRECTION
) {
        const uint8_t * IP = NULL;
        size_t IP_LENGTH = 0;

        if (DIRECTION) *DIRECTION = 0;
        if (!CAPTURE || !ADDRESS) return ISFALSE;

        if (
                FAMILY == NOSIX_ADDRESS_IPV4
                && !(CAPTURE->flags & NOSIX_CAPTURE_IPV4_LOCAL)
                && CAPTURE->frame.data
                && CAPTURE->frame.length >= 42
        ) {
                const uint8_t * FRAME = CAPTURE->frame.data;
                size_t L2 = 14;
                uint16_t ETHERTYPE = (uint16_t)(
                        ((uint16_t)FRAME[12] << 8) | FRAME[13]
                );
                uint8_t TARGET[4];

                if (ETHERTYPE == 0x8100 || ETHERTYPE == 0x88A8) {
                        if (CAPTURE->frame.length < 46) return ISFALSE;
                        ETHERTYPE = (uint16_t)(
                                ((uint16_t)FRAME[16] << 8) | FRAME[17]
                        );
                        L2 = 18;
                }

                if (
                        ETHERTYPE == 0x0806
                        && CAPTURE->frame.length >= L2 + 28
                        && inet_pton(AF_INET, ADDRESS, TARGET) == 1
                ) {
                        const uint8_t * ARP = FRAME + L2;

                        if (
                                ARP[2] == 0x08
                                && ARP[3] == 0x00
                                && ARP[4] == 6
                                && ARP[5] == 4
                        ) {
                                if (memcmp(ARP + 14, TARGET, sizeof(TARGET)) == MATCH) {
                                        if (DIRECTION) *DIRECTION = -1;
                                        return ISTRUE;
                                }

                                if (memcmp(ARP + 24, TARGET, sizeof(TARGET)) == MATCH) {
                                        if (DIRECTION) *DIRECTION = 1;
                                        return ISTRUE;
                                }
                        }
                }
        }

        if (
                kportscan_capture_l3(
                        CAPTURE,
                        FAMILY,
                        &IP,
                        &IP_LENGTH
                ) != NORMAL
        ) {
                return ISFALSE;
        }

        if (FAMILY == NOSIX_ADDRESS_IPV4) {
                uint8_t TARGET[4];

                if (
                        IP_LENGTH < 20
                        || (IP[0] >> 4) != 4
                        || IP[9] != NOSIX_IPPROTO_ICMP
                        || inet_pton(AF_INET, ADDRESS, TARGET) != 1
                ) {
                        return ISFALSE;
                }

                if (memcmp(IP + 12, TARGET, sizeof(TARGET)) == MATCH) {
                        if (DIRECTION) *DIRECTION = -1;
                        return ISTRUE;
                }

                if (memcmp(IP + 16, TARGET, sizeof(TARGET)) == MATCH) {
                        if (DIRECTION) *DIRECTION = 1;
                        return ISTRUE;
                }

                return ISFALSE;
        }

        if (FAMILY == NOSIX_ADDRESS_IPV6) {
                uint8_t TARGET[16];
                uint8_t TYPE;

                if (
                        IP_LENGTH < 40
                        || (IP[0] >> 4) != 6
                        || IP[6] != NOSIX_IPPROTO_ICMPV6
                        || inet_pton(AF_INET6, ADDRESS, TARGET) != 1
                ) {
                        return ISFALSE;
                }

                if (memcmp(IP + 8, TARGET, sizeof(TARGET)) == MATCH) {
                        if (DIRECTION) *DIRECTION = -1;
                        return ISTRUE;
                }

                if (memcmp(IP + 24, TARGET, sizeof(TARGET)) == MATCH) {
                        if (DIRECTION) *DIRECTION = 1;
                        return ISTRUE;
                }

                if (IP_LENGTH < 64) return ISFALSE;
                TYPE = IP[40];

                if (
                        (TYPE == 135 || TYPE == 136)
                        && memcmp(IP + 48, TARGET, sizeof(TARGET)) == MATCH
                ) {
                        if (DIRECTION) *DIRECTION = TYPE == 135 ? 1 : -1;
                        return ISTRUE;
                }
        }

        return ISFALSE;
}

static int kbanner_append_capture(
        const char * PATH,
        const nosix_capture_t * CAPTURE,
        nosix_address_family_t FAMILY
) {
        FILE * FILE_HANDLE;
        KPCAP_GLOBAL_HEADER GLOBAL;
        KPCAP_PACKET_HEADER HEADER;
        const uint8_t * DATA;
        size_t CAPTURE_LENGTH;
        size_t WIRE_LENGTH;
        uint64_t TIMESTAMP_NS;

        if (
                !PATH
                || !CAPTURE
                || !CAPTURE->frame.data
                || CAPTURE->frame.length == 0
        ) {
                return ABNORMAL;
        }

        FILE_HANDLE = fopen(PATH, "rb+");
        if (!FILE_HANDLE) {
                return ABNORMAL;
        }

        memset(&GLOBAL, 0x00, sizeof(GLOBAL));
        if (fread(&GLOBAL, sizeof(GLOBAL), 1, FILE_HANDLE) != 1) {
                fclose(FILE_HANDLE);
                return ABNORMAL;
        }

        DATA = CAPTURE->frame.data;
        CAPTURE_LENGTH = CAPTURE->frame.length;
        WIRE_LENGTH = CAPTURE->wire_length
                ? CAPTURE->wire_length
                : CAPTURE->frame.length;

        if (GLOBAL.NETWORK == KPCAP_LINKTYPE_RAW_IPV4) {
                const uint8_t * IP = NULL;
                size_t IP_LENGTH = 0;

                if (
                        FAMILY != NOSIX_ADDRESS_IPV4
                        || kportscan_capture_l3(
                                CAPTURE,
                                FAMILY,
                                &IP,
                                &IP_LENGTH
                        ) != NORMAL
                ) {
                        fclose(FILE_HANDLE);
                        return ABNORMAL;
                }

                DATA = IP;
                CAPTURE_LENGTH = IP_LENGTH;
                WIRE_LENGTH = IP_LENGTH;
        } else if (GLOBAL.NETWORK != KPCAP_LINKTYPE_ETHERNET) {
                fclose(FILE_HANDLE);
                return ABNORMAL;
        }

        if (CAPTURE_LENGTH > UINT32_MAX || WIRE_LENGTH > UINT32_MAX) {
                fclose(FILE_HANDLE);
                return ABNORMAL;
        }

        memset(&HEADER, 0x00, sizeof(HEADER));
        TIMESTAMP_NS = CAPTURE->timestamp_ns
                ? CAPTURE->timestamp_ns
                : kscan_now_ns();
        HEADER.TIMESTAMP_SECONDS = (uint32_t)(TIMESTAMP_NS / 1000000000ULL);
        HEADER.TIMESTAMP_NANOSECONDS = (uint32_t)(TIMESTAMP_NS % 1000000000ULL);
        HEADER.CAPTURE_LENGTH = (uint32_t)CAPTURE_LENGTH;
        HEADER.WIRE_LENGTH = (uint32_t)WIRE_LENGTH;

        if (
                fseek(FILE_HANDLE, 0, SEEK_END) != NORMAL
                || fwrite(&HEADER, sizeof(HEADER), 1, FILE_HANDLE) != 1
                || fwrite(DATA, CAPTURE_LENGTH, 1, FILE_HANDLE) != 1
        ) {
                fclose(FILE_HANDLE);
                return ABNORMAL;
        }

        fflush(FILE_HANDLE);
        fclose(FILE_HANDLE);
        return NORMAL;
}

static void kbanner_capture_stream_transaction(
        _carry_forward * _prog_data,
        const unsigned char * TID,
        const char * ADDRESS,
        nosix_address_family_t FAMILY,
        uint16_t PORT,
        uint64_t SCAN_START_NS
) {
        char PCAP_PATH[MAX_PATH];
        uint8_t CATCH[OUT_BLOCK];
        nosix_capture_t CAPTURE;
        uint16_t LOCAL_PORT = 0;
        uint64_t APPENDED = 0;
        int8_t PCAP_AVAILABLE = ISFALSE;
        KWIRE_DEADLINE DEADLINE;

        if (
                !_prog_data
                || !_prog_data->nosix_net
                || !TID
                || !ADDRESS
                || PORT == 0
        ) {
                return;
        }

        memset(PCAP_PATH, 0x00, sizeof(PCAP_PATH));
        if (
                kbanner_find_port_pcap(
                        PCAP_PATH,
                        sizeof(PCAP_PATH),
                        _prog_data,
                        TID,
                        FAMILY,
                        PORT,
                        SCAN_START_NS
                ) == NORMAL
        ) {
                PCAP_AVAILABLE = ISTRUE;
        } else {
                kui_add_line_and_render(
                        NOTICE_ERROR
                        "Unable to locate TCP/%u packet capture for banner evidence.",
                        PORT
                );
        }

        memset(CATCH, 0x00, sizeof(CATCH));
        memset(&CAPTURE, 0x00, sizeof(CAPTURE));
        CAPTURE.frame.data = CATCH;
        CAPTURE.frame.capacity = sizeof(CATCH);

        if (kwire_deadline_start(&DEADLINE, _prog_data->gprof.rx_timeout_ms) != NORMAL) {
                return;
        }

        for (unsigned int INDEX = 0; INDEX < 8192U; INDEX++) {
                nosix_status_t STATUS;
                int32_t REMAINING = kwire_deadline_remaining_ms(&DEADLINE);
                if (REMAINING <= 0) break;
                int8_t DIRECTION = 0;
                int8_t TRANSPORT_MATCH;
                int8_t SUPPORTING_MATCH;

                CAPTURE.frame.length = 0;
                CAPTURE.wire_length = 0;
                CAPTURE.timestamp_ns = 0;
                CAPTURE.interface_index = 0;
                CAPTURE.flags = 0;

                STATUS = nosix_read_timeout(
                        _prog_data->nosix_net,
                        &CAPTURE,
                        REMAINING < 75 ? REMAINING : 75
                );

                if (STATUS == NOSIX_TIMEOUT) {
                        break;
                }

                if (STATUS != NOSIX_OK && STATUS != NOSIX_TRUNCATED) {
                        break;
                }


                TRANSPORT_MATCH = kbanner_capture_matches_stream(
                        &CAPTURE,
                        ADDRESS,
                        FAMILY,
                        PORT,
                        &LOCAL_PORT,
                        &DIRECTION
                );

                SUPPORTING_MATCH = ISFALSE;
                if (TRANSPORT_MATCH != ISTRUE) {
                        SUPPORTING_MATCH = kbanner_capture_matches_supporting(
                                &CAPTURE,
                                ADDRESS,
                                FAMILY,
                                &DIRECTION
                        );
                }

                if (TRANSPORT_MATCH != ISTRUE && SUPPORTING_MATCH != ISTRUE) {
                        continue;
                }

                {
                        uint64_t WIRE_BYTES = CAPTURE.wire_length
                                ? CAPTURE.wire_length
                                : CAPTURE.frame.length;

                        if (DIRECTION > 0) {
                                _prog_data->total_tx_bytes += WIRE_BYTES;
                        } else if (DIRECTION < 0) {
                                _prog_data->total_rx_bytes += WIRE_BYTES;
                        }
                }

                if (
                        TRANSPORT_MATCH == ISTRUE
                        && PCAP_AVAILABLE == ISTRUE
                        && kbanner_append_capture(
                                PCAP_PATH,
                                &CAPTURE,
                                FAMILY
                        ) == NORMAL
                ) {
                        APPENDED++;
                }
        }

        if (PCAP_AVAILABLE == ISTRUE && APPENDED == 0) {
                kui_add_line_and_render(
                        NOTICE_WARNING
                        "TCP/%u banner transaction produced no capturable packets for its port PCAP.",
                        PORT
                );
        }
}

static void kbanner_close_and_capture(
        _carry_forward * _prog_data,
        const unsigned char * TID,
        const char * ADDRESS,
        nosix_address_family_t FAMILY,
        uint16_t PORT,
        uint64_t SCAN_START_NS,
        nosix_stream_t ** STREAM
) {
        if (STREAM && *STREAM) {
                (void)nosix_stream_close(STREAM);
        }

        kbanner_capture_stream_transaction(
                _prog_data,
                TID,
                ADDRESS,
                FAMILY,
                PORT,
                SCAN_START_NS
        );
}

static int32_t kbanner_rx_timeout(
        const _carry_forward * _prog_data
) {
        if (
                _prog_data
                && _prog_data->gprof.rx_timeout_ms > 0
        ) {
                return _prog_data->gprof.rx_timeout_ms;
        }

        return KBANNER_CONNECT_TIMEOUT_MS;
}

static int kbanner_enrich_port(
        _carry_forward * _prog_data,
        const unsigned char * TID,
        const char * ADDRESS,
        nosix_address_family_t FAMILY,
        const nosix_address_t * DESTINATION,
        uint16_t PORT,
        uint64_t SCAN_START_NS
) {
        nosix_stream_t * STREAM = NULL;
        nosix_status_t STATUS;
        uint8_t BANNER[KBANNER_MAX_BYTES];
        size_t BANNER_LENGTH = 0;
        const char * SERVICE;

        if (
                !_prog_data
                || !TID
                || !ADDRESS
                || !DESTINATION
                || PORT == 0
                || SCAN_START_NS == 0
        ) {
                return ABNORMAL;
        }

        memset(BANNER, 0x00, sizeof(BANNER));
        kbanner_capture_flush(_prog_data);

        kui_add_line_and_render(
                NOTICE_TRANSMISSION
                "Initiating TCP/%u banner connection to "
                ANSI_COLOR_CYAN
                "%s"
                ANSI_COLOR_RESET
                ".",
                PORT,
                ADDRESS
        );

        STATUS = nosix_stream_open(
                _prog_data->nosix_net,
                &STREAM,
                DESTINATION,
                PORT,
                kbanner_connect_timeout(_prog_data)
        );

        if (STATUS != NOSIX_OK) {
                // @@ Failed connects can still emit SYN/RST/ICMP traffic.
                // Drain and account whatever NOSIX captured for this attempt.
                kbanner_close_and_capture(
                        _prog_data, TID, ADDRESS, FAMILY,
                        PORT, SCAN_START_NS, &STREAM
                );
                return ABNORMAL;
        }

        STATUS = kbanner_collect(
                STREAM,
                BANNER,
                sizeof(BANNER),
                &BANNER_LENGTH,
                kbanner_rx_timeout(_prog_data)
        );

        if (
                STATUS != NOSIX_OK
                && STATUS != NOSIX_TIMEOUT
                && STATUS != NOSIX_EOF
        ) {
                kbanner_close_and_capture(
                        _prog_data, TID, ADDRESS, FAMILY,
                        PORT, SCAN_START_NS, &STREAM
                );
                return ABNORMAL;
        }

        if (BANNER_LENGTH > 0) {
                SERVICE = kbanner_classify(
                        BANNER,
                        BANNER_LENGTH,
                        PORT,
                        KBANNER_PROBE_NONE
                );

                (void)kbanner_store(
                        _prog_data,
                        TID,
                        FAMILY,
                        PORT,
                        SERVICE,
                        "PASSIVE",
                        KBANNER_PROBE_NONE,
                        BANNER,
                        BANNER_LENGTH
                );

                kbanner_emit(
                        PORT,
                        SERVICE,
                        "PASSIVE",
                        KBANNER_PROBE_NONE,
                        BANNER_LENGTH
                );
        }

        kbanner_close_and_capture(
                _prog_data, TID, ADDRESS, FAMILY,
                PORT, SCAN_START_NS, &STREAM
        );
        return NORMAL;
}

int kbanner_enrich_tcp(
        _carry_forward * _prog_data,
        const unsigned char * TID,
        const char * ADDRESS,
        nosix_address_family_t FAMILY,
        uint64_t SCAN_START_NS
) {
        uint8_t * OPEN_PORTS;
        uint64_t OPEN_TOTAL;
        nosix_address_t DESTINATION;
        int STATUS = NORMAL;

        if (
                !_prog_data
                || !TID
                || !ADDRESS
                || !_prog_data->nosix_net
                || SCAN_START_NS == 0
        ) {
                return ABNORMAL;
        }

        OPEN_PORTS = calloc(KBANNER_BITMAP_BYTES, 1);
        if (!OPEN_PORTS) {
                return ABNORMAL;
        }

        OPEN_TOTAL = kbanner_load_open_ports(
                _prog_data,
                TID,
                FAMILY,
                SCAN_START_NS,
                OPEN_PORTS
        );

        if (OPEN_TOTAL == 0) {
                free(OPEN_PORTS);
                return NORMAL;
        }

        if (
                kbanner_destination(
                        &DESTINATION,
                        ADDRESS,
                        FAMILY
                ) != NORMAL
        ) {
                free(OPEN_PORTS);
                return ABNORMAL;
        }

        kui_add_line_and_render(
                NOTICE_INFO
                "Enriching " ANSI_COLOR_CYAN "%llu" ANSI_COLOR_RESET
                " open TCP service%s.",
                (unsigned long long)OPEN_TOTAL,
                OPEN_TOTAL == 1 ? "" : "s"
        );

        for (unsigned int PORT = 1; PORT < MAX_PORTS; PORT++) {
                if (
                        kbanner_bitmap_has(
                                OPEN_PORTS,
                                (uint16_t)PORT
                        ) != ISTRUE
                ) {
                        continue;
                }

                if (
                        kbanner_enrich_port(
                                _prog_data,
                                TID,
                                ADDRESS,
                                FAMILY,
                                &DESTINATION,
                                (uint16_t)PORT,
                                SCAN_START_NS
                        ) != NORMAL
                ) {
                        STATUS = ABNORMAL;
                }
        }

        free(OPEN_PORTS);
        return STATUS;
}

static int kbanner_hex_nibble(char VALUE) {
        if (VALUE >= '0' && VALUE <= '9') {
                return VALUE - '0';
        }
        if (VALUE >= 'A' && VALUE <= 'F') {
                return 10 + VALUE - 'A';
        }
        if (VALUE >= 'a' && VALUE <= 'f') {
                return 10 + VALUE - 'a';
        }
        return -1;
}

static size_t kbanner_decode_hex(
        const char * HEX,
        uint8_t * OUT,
        size_t OUT_SIZE
) {
        size_t LENGTH = 0;

        if (!HEX || !OUT || OUT_SIZE == 0) {
                return 0;
        }

        while (
                HEX[0] != 0x00
                && HEX[1] != 0x00
                && LENGTH < OUT_SIZE
        ) {
                int HIGH = kbanner_hex_nibble(HEX[0]);
                int LOW = kbanner_hex_nibble(HEX[1]);

                if (HIGH < 0 || LOW < 0) {
                        break;
                }

                OUT[LENGTH++] = (uint8_t)((HIGH << 4) | LOW);
                HEX += 2;
        }

        return LENGTH;
}

static void kbanner_copy_field(
        char * OUT,
        size_t OUT_SIZE,
        const uint8_t * DATA,
        size_t START,
        size_t END
) {
        size_t POS = 0;

        if (!OUT || OUT_SIZE == 0 || !DATA || END < START) {
                return;
        }

        memset(OUT, 0x00, OUT_SIZE);

        while (START < END && POS + 1 < OUT_SIZE) {
                uint8_t BYTE = DATA[START++];

                if (BYTE == '\r' || BYTE == '\n' || BYTE == '\t') {
                        BYTE = ' ';
                }

                OUT[POS++] = isprint((unsigned char)BYTE)
                        ? (char)BYTE
                        : '.';
        }

        while (POS > 0 && OUT[POS - 1] == ' ') {
                OUT[--POS] = 0x00;
        }
}

static size_t kbanner_find_ci(
        const uint8_t * DATA,
        size_t LENGTH,
        const char * NEEDLE
) {
        size_t NEEDLE_LENGTH;

        if (!DATA || !NEEDLE) {
                return SIZE_MAX;
        }

        NEEDLE_LENGTH = strlen(NEEDLE);
        if (NEEDLE_LENGTH == 0 || NEEDLE_LENGTH > LENGTH) {
                return SIZE_MAX;
        }

        for (size_t OFFSET = 0; OFFSET + NEEDLE_LENGTH <= LENGTH; OFFSET++) {
                size_t INDEX = 0;

                while (INDEX < NEEDLE_LENGTH) {
                        unsigned char LEFT = DATA[OFFSET + INDEX];
                        unsigned char RIGHT = (unsigned char)NEEDLE[INDEX];

                        if (tolower(LEFT) != tolower(RIGHT)) {
                                break;
                        }
                        INDEX++;
                }

                if (INDEX == NEEDLE_LENGTH) {
                        return OFFSET;
                }
        }

        return SIZE_MAX;
}

static void kbanner_display_http(
        const KBANNER_EVIDENCE * RECORD
) {
        char FIELD[256];
        size_t END = 0;
        size_t SERVER;
        size_t TITLE;

        if (!RECORD || RECORD->BANNER_BYTES == 0) {
                return;
        }

        while (
                END < RECORD->BANNER_BYTES
                && RECORD->BANNER[END] != '\r'
                && RECORD->BANNER[END] != '\n'
        ) {
                END++;
        }

        memset(FIELD, 0x00, sizeof(FIELD));
        kbanner_copy_field(
                FIELD,
                sizeof(FIELD),
                RECORD->BANNER,
                0,
                END
        );
        if (FIELD[0] != 0x00) {
                kui_add_line(" RESPONSE:   %s", FIELD);
        }

        SERVER = kbanner_find_ci(
                RECORD->BANNER,
                RECORD->BANNER_BYTES,
                "\nServer:"
        );

        if (SERVER != SIZE_MAX) {
                size_t START = SERVER + 8;
                size_t STOP = START;

                while (
                        START < RECORD->BANNER_BYTES
                        && (RECORD->BANNER[START] == ' ' || RECORD->BANNER[START] == '\t')
                ) {
                        START++;
                }
                STOP = START;
                while (
                        STOP < RECORD->BANNER_BYTES
                        && RECORD->BANNER[STOP] != '\r'
                        && RECORD->BANNER[STOP] != '\n'
                ) {
                        STOP++;
                }

                memset(FIELD, 0x00, sizeof(FIELD));
                kbanner_copy_field(
                        FIELD,
                        sizeof(FIELD),
                        RECORD->BANNER,
                        START,
                        STOP
                );
                if (FIELD[0] != 0x00) {
                        kui_add_line(" SERVER:     %s", FIELD);
                }
        }

        TITLE = kbanner_find_ci(
                RECORD->BANNER,
                RECORD->BANNER_BYTES,
                "<title>"
        );

        if (TITLE != SIZE_MAX) {
                size_t START = TITLE + 7;
                size_t RELATIVE_END = kbanner_find_ci(
                        RECORD->BANNER + START,
                        RECORD->BANNER_BYTES - START,
                        "</title>"
                );

                if (RELATIVE_END != SIZE_MAX) {
                        memset(FIELD, 0x00, sizeof(FIELD));
                        kbanner_copy_field(
                                FIELD,
                                sizeof(FIELD),
                                RECORD->BANNER,
                                START,
                                START + RELATIVE_END
                        );
                        if (FIELD[0] != 0x00) {
                                kui_add_line(" TITLE:      %s", FIELD);
                        }
                }
        }
}

static void kbanner_display_bytes(
        const KBANNER_EVIDENCE * RECORD
) {
        char ASCII[KBANNER_ASCII_BYTES + 1];
        size_t ASCII_LENGTH;
        size_t DISPLAY_LENGTH;

        if (!RECORD || RECORD->BANNER_BYTES == 0) {
                return;
        }

        memset(ASCII, 0x00, sizeof(ASCII));
        ASCII_LENGTH = RECORD->BANNER_BYTES < KBANNER_ASCII_BYTES
                ? RECORD->BANNER_BYTES
                : KBANNER_ASCII_BYTES;

        kbanner_copy_field(
                ASCII,
                sizeof(ASCII),
                RECORD->BANNER,
                0,
                ASCII_LENGTH
        );

        kui_add_line(" ASCII:      %s", ASCII);
        kui_add_line(" BYTES:");

        DISPLAY_LENGTH = RECORD->BANNER_BYTES < KBANNER_DISPLAY_BYTES
                ? RECORD->BANNER_BYTES
                : KBANNER_DISPLAY_BYTES;

        for (size_t OFFSET = 0; OFFSET < DISPLAY_LENGTH; OFFSET += 16) {
                char LINE[128];
                size_t POS = 0;

                memset(LINE, 0x00, sizeof(LINE));
                POS += (size_t)snprintf(
                        LINE + POS,
                        sizeof(LINE) - POS,
                        " %04zx  ",
                        OFFSET
                );

                for (
                        size_t INDEX = 0;
                        INDEX < 16 && OFFSET + INDEX < DISPLAY_LENGTH;
                        INDEX++
                ) {
                        POS += (size_t)snprintf(
                                LINE + POS,
                                sizeof(LINE) - POS,
                                "%02X ",
                                RECORD->BANNER[OFFSET + INDEX]
                        );
                }

                kui_add_line("%s", LINE);
        }

        if (DISPLAY_LENGTH < RECORD->BANNER_BYTES) {
                kui_add_line(
                        NOTICE_INFO
                        "%zu additional banner bytes omitted.",
                        RECORD->BANNER_BYTES - DISPLAY_LENGTH
                );
        }
}

static int kbanner_load_latest(
        _carry_forward * _prog_data,
        const unsigned char * TID,
        unsigned int FAMILY,
        unsigned int PORT,
        KBANNER_EVIDENCE * RECORD
) {
        char PATH[MAX_PATH];
        FILE * FILE_HANDLE;
        char * LINE = NULL;
        size_t CAPACITY = 0;
        int FOUND = ABNORMAL;

        if (!_prog_data || !TID || !RECORD || PORT == 0) {
                return ABNORMAL;
        }

        memset(RECORD, 0x00, sizeof(*RECORD));
        memset(PATH, 0x00, sizeof(PATH));

        if (
                kbanner_path(
                        PATH,
                        sizeof(PATH),
                        _prog_data,
                        TID,
                        ".services"
                ) != NORMAL
        ) {
                return ABNORMAL;
        }

        FILE_HANDLE = fopen(PATH, "r");
        if (!FILE_HANDLE) {
                return ABNORMAL;
        }

        while (getline(&LINE, &CAPACITY, FILE_HANDLE) >= 0) {
                char * SAVE = NULL;
                char * FIELD;
                char * FIELDS[9];
                size_t FIELD_COUNT = 0;
                unsigned long long TIMESTAMP_NS;
                unsigned long PARSED_AF;
                unsigned long PARSED_PORT;
                unsigned long PARSED_BYTES;
                char * END = NULL;

                if (!LINE || LINE[0] == '#') {
                        continue;
                }

                LINE[strcspn(LINE, "\r\n")] = 0x00;

                FIELD = strtok_r(LINE, "\t", &SAVE);
                while (FIELD && FIELD_COUNT < 9) {
                        FIELDS[FIELD_COUNT++] = FIELD;
                        FIELD = strtok_r(NULL, "\t", &SAVE);
                }

                if (FIELD_COUNT != 9 || strcmp(FIELDS[1], "TCP") != MATCH) {
                        continue;
                }

                TIMESTAMP_NS = strtoull(FIELDS[0], &END, 10);
                if (!END || *END != 0x00) {
                        continue;
                }

                PARSED_AF = strtoul(FIELDS[2], &END, 10);
                if (!END || *END != 0x00) {
                        continue;
                }

                PARSED_PORT = strtoul(FIELDS[3], &END, 10);
                if (!END || *END != 0x00) {
                        continue;
                }

                PARSED_BYTES = strtoul(FIELDS[7], &END, 10);
                if (!END || *END != 0x00) {
                        continue;
                }

                if (
                        PARSED_AF != FAMILY
                        || PARSED_PORT != PORT
                        || (
                                RECORD->PRESENT == ISTRUE
                                && TIMESTAMP_NS <= RECORD->TIMESTAMP_NS
                        )
                ) {
                        continue;
                }

                memset(RECORD, 0x00, sizeof(*RECORD));
                RECORD->TIMESTAMP_NS = (uint64_t)TIMESTAMP_NS;
                RECORD->FAMILY = (uint8_t)PARSED_AF;
                RECORD->PORT = (uint16_t)PARSED_PORT;
                snprintf(RECORD->SERVICE, sizeof(RECORD->SERVICE), "%s", FIELDS[4]);
                snprintf(RECORD->MODE, sizeof(RECORD->MODE), "%s", FIELDS[5]);
                snprintf(RECORD->PROBE, sizeof(RECORD->PROBE), "%s", FIELDS[6]);
                RECORD->BANNER_BYTES = kbanner_decode_hex(
                        FIELDS[8],
                        RECORD->BANNER,
                        sizeof(RECORD->BANNER)
                );

                if (PARSED_BYTES < RECORD->BANNER_BYTES) {
                        RECORD->BANNER_BYTES = (size_t)PARSED_BYTES;
                }

                RECORD->PRESENT = ISTRUE;
                FOUND = NORMAL;
        }

        free(LINE);
        fclose(FILE_HANDLE);
        return FOUND;
}

void kbanner_display_port(
        _carry_forward * _prog_data,
        const unsigned char * TID,
        unsigned int PROTOCOL_INDEX,
        unsigned int FAMILY_INDEX,
        unsigned int PORT
) {
        KBANNER_EVIDENCE RECORD;
        unsigned int FAMILY;

        if (!_prog_data || !TID || PROTOCOL_INDEX != 0U || PORT == 0) {
                return;
        }

        FAMILY = FAMILY_INDEX == 0U ? 4U : 6U;

        if (
                kbanner_load_latest(
                        _prog_data,
                        TID,
                        FAMILY,
                        PORT,
                        &RECORD
                ) != NORMAL
        ) {
                kui_add_line(" SERVICE:    UNKNOWN");
                kui_add_line(" SOURCE:     NONE");
                kui_add_line(" BANNER:     NOT CAPTURED");
                return;
        }

        kui_add_line(" SERVICE:    %s", RECORD.SERVICE);
        kui_add_line(" SOURCE:     %s", RECORD.MODE);

        if (strcmp(RECORD.PROBE, "NONE") != MATCH) {
                kui_add_line(" PROBE:      %s", RECORD.PROBE);
        }

        kui_add_line(" BANNER:     %zu bytes", RECORD.BANNER_BYTES);

        if (
                strcmp(RECORD.SERVICE, "HTTP") == MATCH
                || strcmp(RECORD.SERVICE, "HTTP?") == MATCH
        ) {
                kbanner_display_http(&RECORD);
        } else if (
                strcmp(RECORD.SERVICE, "TLS") == MATCH
                || strcmp(RECORD.SERVICE, "TLS?") == MATCH
        ) {
                if (RECORD.BANNER_BYTES >= 3) {
                        const char * RESPONSE = RECORD.BANNER[0] == 0x16
                                ? "TLS HANDSHAKE"
                                : RECORD.BANNER[0] == 0x15
                                        ? "TLS ALERT"
                                        : "TLS RECORD";

                        kui_add_line(" RESPONSE:   %s", RESPONSE);
                        kui_add_line(
                                " RECORD:     0x%02X / 0x%02X%02X",
                                RECORD.BANNER[0],
                                RECORD.BANNER[1],
                                RECORD.BANNER[2]
                        );
                }
        }

        kbanner_display_bytes(&RECORD);
}
