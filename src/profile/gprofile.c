// Copyright 2026 Jamison A. Drapeau
#define _POSIX_C_SOURCE 200809L
//==========================================================
// Largely AI generated but refined based on developer input
//==========================================================

// Required
#include "gprofile.h"
#include "helpers.h"

// BSD Direct Includes, direct compat. x-platform
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// the rest of the things
#include <ctype.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// Data size outline
#define GPROFILE_LINE_BLOCK       1024
#define GPROFILE_KEY_BLOCK         128

// Last parser/runtime error for the command and startup presentation layers.
static char GPROFILE_ERROR[MAX_BLOCK];

static void gprofile_set_error(const char * FORMAT, ...) {
        va_list AP;

        memset(GPROFILE_ERROR, 0x00, sizeof(GPROFILE_ERROR));

        if (!FORMAT) {
                return;
        }

        va_start(AP, FORMAT);
        vsnprintf(GPROFILE_ERROR, sizeof(GPROFILE_ERROR), FORMAT, AP);
        va_end(AP);
}

const char * gprofile_last_error(void) {
        if (GPROFILE_ERROR[0] == 0x00) {
                return "Unknown global profile failure.";
        }

        return GPROFILE_ERROR;
}

// Expected sections
#define GPROFILE_SECTION_PROFILE  (1U << 0)
#define GPROFILE_SECTION_TX       (1U << 1)
#define GPROFILE_SECTION_RX       (1U << 2)
#define GPROFILE_SECTION_DNS      (1U << 3)
#define GPROFILE_SECTION_LIMITS   (1U << 4)
#define GPROFILE_SECTION_REQUIRED (GPROFILE_SECTION_PROFILE | GPROFILE_SECTION_TX | GPROFILE_SECTION_RX | GPROFILE_SECTION_DNS | GPROFILE_SECTION_LIMITS)

// Expection
#define GPROFILE_FIELD_ALLOW_EMPTY (1U << 0)
#define GPROFILE_FIELD_OPTIONAL    (1U << 1)
#define GPROFILE_FIELD_LEGACY      (1U << 2)

// Profile section definitions
typedef enum {
        GPROFILE_SECTION_UNSET = -1,
        GPROFILE_SECTION_TYPE_PROFILE,
        GPROFILE_SECTION_TYPE_TX,
        GPROFILE_SECTION_TYPE_RX,
        GPROFILE_SECTION_TYPE_DNS,
        GPROFILE_SECTION_TYPE_LIMITS
} gprofile_section_t;

// Profile value definitions
typedef enum {
        GPROFILE_VALUE_STRING = 0,
        GPROFILE_VALUE_BOOL,
        GPROFILE_VALUE_INT,
        GPROFILE_VALUE_UINT,
        GPROFILE_VALUE_U64,
        GPROFILE_VALUE_IP,
        GPROFILE_VALUE_IPV4,
        GPROFILE_VALUE_INTERFACE,
        GPROFILE_VALUE_TX_GENERATION,
        GPROFILE_VALUE_RX_STATE,
        GPROFILE_VALUE_MATCH_POLICY,
        GPROFILE_VALUE_CHECKSUM_MODE
} gprofile_value_type_t;

// Extraction Tuple + metadata
typedef struct {
        gprofile_section_t      SECTION;
        char *                  KEY;
        char *                  VALUE;
        unsigned long           LINE_NUMBER;
} GPROFILE_TUPLE;

// Field definitions
typedef struct {
        gprofile_section_t      SECTION;
        const char *            KEY;
        gprofile_value_type_t   TYPE;
        c_size_t                OFFSET;
        c_size_t                SIZE;
        long long               MIN;
        unsigned long long      MAX;
        unsigned int            FLAGS;
        const char *            SPECIAL_VALUE;
        uint64_t                SEEN_FLAG;
} GPROFILE_FIELD;

// Analysis Tuple
typedef struct {
        unsigned int            SECTIONS;
        uint64_t                FIELDS;
} GPROFILE_ANALYSIS;

// Relational value size definition
#define GPROFILE_MEMBER_SIZE(MEMBER) sizeof(((GLOBAL_PROFILE *)0)->MEMBER)

// Field Sentinals/Definitions
static const GPROFILE_FIELD GPROFILE_FIELDS[] = {
        {
                GPROFILE_SECTION_TYPE_PROFILE,
                "name",
                GPROFILE_VALUE_STRING,
                offsetof(GLOBAL_PROFILE, profile_name),
                GPROFILE_MEMBER_SIZE(profile_name),
                0,
                0,
                0,
                NULL,
                (1ULL << 0)
        },
        {
                GPROFILE_SECTION_TYPE_PROFILE,
                "version",
                GPROFILE_VALUE_INT,
                offsetof(GLOBAL_PROFILE, profile_schema_version),
                GPROFILE_MEMBER_SIZE(profile_schema_version),
                1,
                9999,
                0,
                NULL,
                (1ULL << 1)
        },
        {
                GPROFILE_SECTION_TYPE_PROFILE,
                "scope",
                GPROFILE_VALUE_STRING,
                offsetof(GLOBAL_PROFILE, scope),
                GPROFILE_MEMBER_SIZE(scope),
                0,
                0,
                0,
                NULL,
                (1ULL << 2)
        },
        {
                GPROFILE_SECTION_TYPE_TX,
                "interface",
                GPROFILE_VALUE_INTERFACE,
                offsetof(GLOBAL_PROFILE, tx_interface),
                GPROFILE_MEMBER_SIZE(tx_interface),
                0,
                0,
                0,
                "auto",
                (1ULL << 3)
        },
        {
                GPROFILE_SECTION_TYPE_TX,
                "source_ip",
                GPROFILE_VALUE_IP,
                offsetof(GLOBAL_PROFILE, tx_source_ip),
                GPROFILE_MEMBER_SIZE(tx_source_ip),
                0,
                0,
                GPROFILE_FIELD_ALLOW_EMPTY,
                NULL,
                (1ULL << 4)
        },
        {
                GPROFILE_SECTION_TYPE_TX,
                "gateway",
                GPROFILE_VALUE_IP,
                offsetof(GLOBAL_PROFILE, tx_gateway),
                GPROFILE_MEMBER_SIZE(tx_gateway),
                0,
                0,
                GPROFILE_FIELD_ALLOW_EMPTY,
                NULL,
                (1ULL << 5)
        },
        {
                GPROFILE_SECTION_TYPE_TX,
                "strict",
                GPROFILE_VALUE_BOOL,
                offsetof(GLOBAL_PROFILE, tx_strict),
                GPROFILE_MEMBER_SIZE(tx_strict),
                0,
                0,
                0,
                NULL,
                (1ULL << 6)
        },
        {
                GPROFILE_SECTION_TYPE_TX,
                "ttl",
                GPROFILE_VALUE_INT,
                offsetof(GLOBAL_PROFILE, tx_ipv4_ttl),
                GPROFILE_MEMBER_SIZE(tx_ipv4_ttl),
                1,
                255,
                0,
                NULL,
                (1ULL << 7)
        },
        {
                GPROFILE_SECTION_TYPE_TX,
                "hop_limit",
                GPROFILE_VALUE_INT,
                offsetof(GLOBAL_PROFILE, tx_ipv6_hop_limit),
                GPROFILE_MEMBER_SIZE(tx_ipv6_hop_limit),
                1,
                255,
                0,
                NULL,
                (1ULL << 8)
        },
        {
                GPROFILE_SECTION_TYPE_TX,
                "ip_id_mode",
                GPROFILE_VALUE_TX_GENERATION,
                offsetof(GLOBAL_PROFILE, tx_ipv4_id_generation),
                GPROFILE_MEMBER_SIZE(tx_ipv4_id_generation),
                0,
                0,
                0,
                NULL,
                (1ULL << 9)
        },
        {
                GPROFILE_SECTION_TYPE_TX,
                "tcp_src_port_mode",
                GPROFILE_VALUE_TX_GENERATION,
                offsetof(GLOBAL_PROFILE, tx_tcp_src_port_generation),
                GPROFILE_MEMBER_SIZE(tx_tcp_src_port_generation),
                0,
                0,
                0,
                NULL,
                (1ULL << 10)
        },
        {
                GPROFILE_SECTION_TYPE_TX,
                "checksum_mode",
                GPROFILE_VALUE_CHECKSUM_MODE,
                offsetof(GLOBAL_PROFILE, tx_checksum_mode),
                GPROFILE_MEMBER_SIZE(tx_checksum_mode),
                0,
                0,
                0,
                NULL,
                (1ULL << 11)
        },
        {
                GPROFILE_SECTION_TYPE_RX,
                "interface",
                GPROFILE_VALUE_INTERFACE,
                offsetof(GLOBAL_PROFILE, rx_interface),
                GPROFILE_MEMBER_SIZE(rx_interface),
                0,
                0,
                0,
                "same",
                (1ULL << 12)
        },
        {
                GPROFILE_SECTION_TYPE_RX,
                "timeout_ms",
                GPROFILE_VALUE_INT,
                offsetof(GLOBAL_PROFILE, rx_timeout_ms),
                GPROFILE_MEMBER_SIZE(rx_timeout_ms),
                1,
                INT_MAX,
                0,
                NULL,
                (1ULL << 13)
        },
        {
                GPROFILE_SECTION_TYPE_RX,
                "promiscuous",
                GPROFILE_VALUE_BOOL,
                offsetof(GLOBAL_PROFILE, rx_promiscuous),
                GPROFILE_MEMBER_SIZE(rx_promiscuous),
                0,
                0,
                0,
                NULL,
                (1ULL << 14)
        },
        {
                GPROFILE_SECTION_TYPE_RX,
                "snaplength",
                GPROFILE_VALUE_INT,
                offsetof(GLOBAL_PROFILE, rx_snaplength),
                GPROFILE_MEMBER_SIZE(rx_snaplength),
                1,
                65535,
                0,
                NULL,
                (1ULL << 15)
        },
        {
                GPROFILE_SECTION_TYPE_RX,
                "state",
                GPROFILE_VALUE_RX_STATE,
                offsetof(GLOBAL_PROFILE, rx_state),
                GPROFILE_MEMBER_SIZE(rx_state),
                0,
                0,
                0,
                NULL,
                (1ULL << 16)
        },
        {
                GPROFILE_SECTION_TYPE_RX,
                "dedupe",
                GPROFILE_VALUE_BOOL,
                offsetof(GLOBAL_PROFILE, rx_dedupe),
                GPROFILE_MEMBER_SIZE(rx_dedupe),
                0,
                0,
                0,
                NULL,
                (1ULL << 17)
        },
        {
                GPROFILE_SECTION_TYPE_RX,
                "match_policy",
                GPROFILE_VALUE_MATCH_POLICY,
                offsetof(GLOBAL_PROFILE, rx_match_policy),
                GPROFILE_MEMBER_SIZE(rx_match_policy),
                0,
                0,
                0,
                NULL,
                (1ULL << 18)
        },
        {
                GPROFILE_SECTION_TYPE_DNS,
                "server1",
                GPROFILE_VALUE_IPV4,
                offsetof(GLOBAL_PROFILE, dns_server1),
                GPROFILE_MEMBER_SIZE(dns_server1),
                0,
                0,
                0,
                NULL,
                (1ULL << 20)
        },
        {
                GPROFILE_SECTION_TYPE_DNS,
                "server2",
                GPROFILE_VALUE_IPV4,
                offsetof(GLOBAL_PROFILE, dns_server2),
                GPROFILE_MEMBER_SIZE(dns_server2),
                0,
                0,
                GPROFILE_FIELD_ALLOW_EMPTY,
                NULL,
                (1ULL << 21)
        },
        {
                GPROFILE_SECTION_TYPE_DNS,
                "search_domain",
                GPROFILE_VALUE_STRING,
                offsetof(GLOBAL_PROFILE, dns_search_domain),
                GPROFILE_MEMBER_SIZE(dns_search_domain),
                0,
                0,
                GPROFILE_FIELD_ALLOW_EMPTY,
                NULL,
                (1ULL << 22)
        },
        {
                GPROFILE_SECTION_TYPE_DNS,
                "timeout_ms",
                GPROFILE_VALUE_INT,
                offsetof(GLOBAL_PROFILE, dns_timeout_ms),
                GPROFILE_MEMBER_SIZE(dns_timeout_ms),
                1,
                INT_MAX,
                0,
                NULL,
                (1ULL << 23)
        },
        {
                GPROFILE_SECTION_TYPE_DNS,
                "retries",
                GPROFILE_VALUE_INT,
                offsetof(GLOBAL_PROFILE, dns_retries),
                GPROFILE_MEMBER_SIZE(dns_retries),
                0,
                INT_MAX,
                0,
                NULL,
                (1ULL << 24)
        },
        {
                GPROFILE_SECTION_TYPE_LIMITS,
                "max_pending_transactions",
                GPROFILE_VALUE_UINT,
                offsetof(GLOBAL_PROFILE, limit_max_pending_transactions),
                GPROFILE_MEMBER_SIZE(limit_max_pending_transactions),
                1,
                65535,
                0,
                NULL,
                (1ULL << 26)
        },
        {
                GPROFILE_SECTION_TYPE_LIMITS,
                "max_capture_bytes_per_tx",
                GPROFILE_VALUE_UINT,
                offsetof(GLOBAL_PROFILE, limit_max_capture_bytes_per_tx),
                GPROFILE_MEMBER_SIZE(limit_max_capture_bytes_per_tx),
                1,
                UINT_MAX,
                0,
                NULL,
                (1ULL << 27)
        },
        {
                GPROFILE_SECTION_TYPE_LIMITS,
                "max_capture_bytes_total",
                GPROFILE_VALUE_U64,
                offsetof(GLOBAL_PROFILE, limit_max_capture_bytes_total),
                GPROFILE_MEMBER_SIZE(limit_max_capture_bytes_total),
                1,
                UINT64_MAX,
                0,
                NULL,
                (1ULL << 28)
        },
        {
                GPROFILE_SECTION_TYPE_LIMITS,
                "max_tx_rate_pps",
                GPROFILE_VALUE_UINT,
                offsetof(GLOBAL_PROFILE, limit_max_tx_out_rate_pps),
                GPROFILE_MEMBER_SIZE(limit_max_tx_out_rate_pps),
                1,
                UINT_MAX,
                0,
                NULL,
                (1ULL << 29)
        },
        {
                GPROFILE_SECTION_TYPE_LIMITS,
                "max_rx_rate_pps",
                GPROFILE_VALUE_UINT,
                offsetof(GLOBAL_PROFILE, limit_max_rx_in_rate_pps),
                GPROFILE_MEMBER_SIZE(limit_max_rx_in_rate_pps),
                1,
                UINT_MAX,
                0,
                NULL,
                (1ULL << 30)
        },
        {
                GPROFILE_SECTION_TYPE_LIMITS,
                "max_dns_queries_pending",
                GPROFILE_VALUE_UINT,
                offsetof(GLOBAL_PROFILE, limit_max_dns_queries_pending),
                GPROFILE_MEMBER_SIZE(limit_max_dns_queries_pending),
                1,
                65535,
                0,
                NULL,
                (1ULL << 31)
        },
        {
                GPROFILE_SECTION_TYPE_LIMITS,
                "max_targets_per_batch",
                GPROFILE_VALUE_UINT,
                offsetof(GLOBAL_PROFILE, limit_max_targets_per_batch),
                GPROFILE_MEMBER_SIZE(limit_max_targets_per_batch),
                1,
                65535,
                0,
                NULL,
                (1ULL << 32)
        },
        {
                GPROFILE_SECTION_TYPE_LIMITS,
                "max_scan_workers",
                GPROFILE_VALUE_UINT,
                offsetof(GLOBAL_PROFILE, limit_max_scan_workers),
                GPROFILE_MEMBER_SIZE(limit_max_scan_workers),
                1,
                256,
                GPROFILE_FIELD_OPTIONAL,
                NULL,
                (1ULL << 33)
        }
};

// Total Fields
static const c_size_t GPROFILE_FIELD_COUNT = sizeof(GPROFILE_FIELDS) / sizeof(GPROFILE_FIELDS[0]);

// @@ Return the printable name of a parsed section (AI Generated)
static const char * gprofile_section_name(gprofile_section_t SECTION) {
        switch (SECTION) {
                case GPROFILE_SECTION_TYPE_PROFILE:
                        return "profile";
                case GPROFILE_SECTION_TYPE_TX:
                        return "tx";
                case GPROFILE_SECTION_TYPE_RX:
                        return "rx";
                case GPROFILE_SECTION_TYPE_DNS:
                        return "dns";
                case GPROFILE_SECTION_TYPE_LIMITS:
                        return "limits";
                default:
                        return "unset";
        }
}

// @@ Trim leading and trailing whitespace in-place (AI Generated)
static char * gprofile_trim(char * STRING) {
        char * END;

        if (!STRING) {
                return NULL;
        }

        while (*STRING && isspace((unsigned char)*STRING)) {
                STRING++;
        }

        END = STRING + strlen(STRING);

        while (END > STRING && isspace((unsigned char)*(END - 1))) {
                END--;
        }

        *END = 0x00;
        return STRING;
}

// @@ Remove line endings and comments, then return normalized line content (AI Generated)
static char * gprofile_prepare_line(char * LINE) {
        char * CURSOR;

        if (!LINE) {
                return NULL;
        }

        LINE[strcspn(LINE, "\r\n")] = 0x00;

        for (CURSOR = LINE; *CURSOR; CURSOR++) {
                if (
                        ((*CURSOR == ';') || (*CURSOR == '#'))
                        &&
                        ((CURSOR == LINE) || isspace((unsigned char)*(CURSOR - 1)))
                ) {
                        *CURSOR = 0x00;
                        break;
                }
        }

        return gprofile_trim(LINE);
}

// @@ Detect and discard a profile line that exceeded the parser buffer (AI Generated)
static int gprofile_validate_line_length(FILE * PROFILE_FILE, const char * LINE) {
        int CHARACTER;

        if (!PROFILE_FILE || !LINE) {
                return ABNORMAL;
        }

        if (strchr(LINE, '\n') || feof(PROFILE_FILE)) {
                return NORMAL;
        }

        while ((CHARACTER = fgetc(PROFILE_FILE)) != '\n' && CHARACTER != EOF) {
                // Drain the remainder of the oversized line.
        }

        return ABNORMAL;
}

// @@ Extract and validate a section header
static int gprofile_extract_section(
        char * STRING,
        gprofile_section_t * OUT,
        GPROFILE_ANALYSIS * ANALYSIS
) {
        char * END_SECTION;
        char * NAME;
        gprofile_section_t SECTION;
        unsigned int FLAG;

        if (!STRING || !OUT || !ANALYSIS || STRING[0] != '[') {
                return ABNORMAL;
        }

        END_SECTION = strchr(STRING, ']');

        if (!END_SECTION || END_SECTION[1] != 0x00) {
                return ABNORMAL;
        }

        *END_SECTION = 0x00;
        NAME = gprofile_trim(STRING + 1);

        if (!NAME || NAME[0] == 0x00) {
                return ABNORMAL;
        }

        SECTION = GPROFILE_SECTION_UNSET;
        FLAG = 0;

        if (strcmp(NAME, "profile") == MATCH) {
                SECTION = GPROFILE_SECTION_TYPE_PROFILE;
                FLAG = GPROFILE_SECTION_PROFILE;
        } else if (strcmp(NAME, "tx") == MATCH) {
                SECTION = GPROFILE_SECTION_TYPE_TX;
                FLAG = GPROFILE_SECTION_TX;
        } else if (strcmp(NAME, "rx") == MATCH) {
                SECTION = GPROFILE_SECTION_TYPE_RX;
                FLAG = GPROFILE_SECTION_RX;
        } else if (strcmp(NAME, "dns") == MATCH) {
                SECTION = GPROFILE_SECTION_TYPE_DNS;
                FLAG = GPROFILE_SECTION_DNS;
        } else if (strcmp(NAME, "limits") == MATCH) {
                SECTION = GPROFILE_SECTION_TYPE_LIMITS;
                FLAG = GPROFILE_SECTION_LIMITS;
        } else {
                return ABNORMAL;
        }

        if ((ANALYSIS->SECTIONS & FLAG) != 0) {
                return ABNORMAL;
        }

        ANALYSIS->SECTIONS |= FLAG;
        *OUT = SECTION;

        return NORMAL;
}

// @@ Split a key/value line and return a tuple of normalized views
static int gprofile_extract_tuple(
        char * STRING,
        gprofile_section_t SECTION,
        unsigned long LINE_NUMBER,
        GPROFILE_TUPLE * OUT
) {
        char * EQUALS;
        char * KEY;
        char * VALUE;

        if (!STRING || !OUT || SECTION == GPROFILE_SECTION_UNSET) {
                return ABNORMAL;
        }

        EQUALS = strchr(STRING, '=');

        if (!EQUALS) {
                return ABNORMAL;
        }

        *EQUALS = 0x00;

        KEY = gprofile_trim(STRING);
        VALUE = gprofile_trim(EQUALS + 1);

        if (!KEY || KEY[0] == 0x00 || !VALUE) {
                return ABNORMAL;
        }

        OUT->SECTION = SECTION;
        OUT->KEY = KEY;
        OUT->VALUE = VALUE;
        OUT->LINE_NUMBER = LINE_NUMBER;

        return NORMAL;
}

// @@ Copy a string without silent truncation
static int gprofile_copy_string(unsigned char * DST, c_size_t DST_SIZE, const char * SRC, int8_t ALLOW_EMPTY) {
        c_size_t LENGTH;

        if (!DST || DST_SIZE == 0 || !SRC) {
                return ABNORMAL;
        }

        if (SRC[0] == 0x00 && ALLOW_EMPTY != ISTRUE) {
                return ABNORMAL;
        }

        LENGTH = strnlen(SRC, DST_SIZE);

        if (LENGTH >= DST_SIZE) {
                return ABNORMAL;
        }

        memset(DST, 0x00, DST_SIZE);
        memcpy(DST, SRC, LENGTH);

        return NORMAL;
}

// @@ Parse documented boolean values
static int gprofile_parse_bool(const char * VALUE, int8_t * OUT) {
        if (!VALUE || !OUT) {
                return ABNORMAL;
        }

        if (strcmp(VALUE, "true") == MATCH) {
                *OUT = ISTRUE;
                return NORMAL;
        }

        if (strcmp(VALUE, "false") == MATCH) {
                *OUT = ISFALSE;
                return NORMAL;
        }

        return ABNORMAL;
}

// @@ Parse a signed integer and enforce a range
static int gprofile_parse_int(const char * VALUE, int * OUT, long long MIN, unsigned long long MAX) {
        char * END;
        long long TMP;

        if (!VALUE || !OUT || VALUE[0] == 0x00) {
                return ABNORMAL;
        }

        errno = 0;
        TMP = strtoll(VALUE, &END, 10);

        if (errno != 0 || END == VALUE || *END != 0x00) {
                return ABNORMAL;
        }

        if (TMP < MIN || (unsigned long long)TMP > MAX) {
                return ABNORMAL;
        }

        *OUT = (int)TMP;
        return NORMAL;
}

// @@ Parse a non-zero unsigned integer without accepting signs
static int gprofile_parse_uint(const char * VALUE, unsigned int * OUT, unsigned long long MAX) {
        char * END;
        unsigned long long TMP;

        if (!VALUE || !OUT || VALUE[0] == 0x00 || VALUE[0] == '-' || VALUE[0] == '+') {
                return ABNORMAL;
        }

        errno = 0;
        TMP = strtoull(VALUE, &END, 10);

        if (errno != 0 || END == VALUE || *END != 0x00 || TMP == 0 || TMP > MAX) {
                return ABNORMAL;
        }

        *OUT = (unsigned int)TMP;
        return NORMAL;
}

// @@ Parse a non-zero uint64_t without accepting signs
static int gprofile_parse_u64(const char * VALUE, uint64_t * OUT, unsigned long long MAX) {
        char * END;
        unsigned long long TMP;

        if (!VALUE || !OUT || VALUE[0] == 0x00 || VALUE[0] == '-' || VALUE[0] == '+') {
                return ABNORMAL;
        }

        errno = 0;
        TMP = strtoull(VALUE, &END, 10);

        if (errno != 0 || END == VALUE || *END != 0x00 || TMP == 0 || TMP > MAX) {
                return ABNORMAL;
        }

        *OUT = (uint64_t)TMP;
        return NORMAL;
}

// @@ Validate and copy an IPv4 or IPv6 address
static int gprofile_copy_ip(unsigned char * DST, c_size_t DST_SIZE, const char * VALUE, int8_t ALLOW_EMPTY) {
        struct in_addr ADDRESS4;
        struct in6_addr ADDRESS6;

        if (!DST || DST_SIZE == 0 || !VALUE) {
                return ABNORMAL;
        }

        if (VALUE[0] == 0x00) {
                return gprofile_copy_string(DST, DST_SIZE, VALUE, ALLOW_EMPTY);
        }

        if (
                inet_pton(AF_INET, VALUE, &ADDRESS4) != 1
                && inet_pton(AF_INET6, VALUE, &ADDRESS6) != 1
        ) {
                return ABNORMAL;
        }

        return gprofile_copy_string(DST, DST_SIZE, VALUE, ISFALSE);
}

// @@ Validate and copy an IPv4 address
static int gprofile_copy_ipv4(unsigned char * DST, c_size_t DST_SIZE, const char * VALUE, int8_t ALLOW_EMPTY) {
        struct in_addr ADDRESS;

        if (!DST || DST_SIZE == 0 || !VALUE) {
                return ABNORMAL;
        }

        if (VALUE[0] == 0x00) {
                return gprofile_copy_string(DST, DST_SIZE, VALUE, ALLOW_EMPTY);
        }

        if (inet_pton(AF_INET, VALUE, &ADDRESS) != 1) {
                return ABNORMAL;
        }

        return gprofile_copy_string(DST, DST_SIZE, VALUE, ISFALSE);
}

// @@ Validate portable interface syntax and copy the value
static int gprofile_copy_interface(
        unsigned char * DST,
        c_size_t DST_SIZE,
        const char * VALUE,
        const char * SPECIAL_VALUE
) {
        const unsigned char * CURSOR;

        if (!DST || DST_SIZE == 0 || !VALUE || VALUE[0] == 0x00) {
                return ABNORMAL;
        }

        if (SPECIAL_VALUE && strcmp(VALUE, SPECIAL_VALUE) == MATCH) {
                return gprofile_copy_string(DST, DST_SIZE, VALUE, ISFALSE);
        }

        for (CURSOR = (const unsigned char *)VALUE; *CURSOR; CURSOR++) {
                if (
                        !isalnum(*CURSOR)
                        &&
                        (*CURSOR != '_')
                        &&
                        (*CURSOR != '-')
                        &&
                        (*CURSOR != '.')
                        &&
                        (*CURSOR != ':')
                        &&
                        (*CURSOR != '@')
                ) {
                        return ABNORMAL;
                }
        }

        return gprofile_copy_string(DST, DST_SIZE, VALUE, ISFALSE);
}

// @@ Parse IPv4 ID and TCP source-port generation modes
static int gprofile_parse_tx_generation(const char * VALUE, tx_generation_t * OUT) {
        if (!VALUE || !OUT) {
                return ABNORMAL;
        }

        if (strcmp(VALUE, "random") == MATCH) {
                *OUT = TXG_RANDOM;
                return NORMAL;
        }

        if (strcmp(VALUE, "static") == MATCH) {
                *OUT = TXG_STATIC;
                return NORMAL;
        }

        if (strcmp(VALUE, "increment") == MATCH) {
                *OUT = TXG_INCREMENT;
                return NORMAL;
        }

        return ABNORMAL;
}

// @@ Parse receive state
static int gprofile_parse_rx_state(const char * VALUE, rx_state_t * OUT) {
        if (!VALUE || !OUT) {
                return ABNORMAL;
        }

        if (strcmp(VALUE, "transaction") == MATCH) {
                *OUT = RXS_TRANSACTION;
                return NORMAL;
        }

        if (strcmp(VALUE, "stream") == MATCH) {
                *OUT = RXS_STREAM;
                return NORMAL;
        }

        return ABNORMAL;
}

// @@ Parse receive matching policy
static int gprofile_parse_match_policy(const char * VALUE, match_policy_t * OUT) {
        if (!VALUE || !OUT) {
                return ABNORMAL;
        }

        if (strcmp(VALUE, "strict") == MATCH) {
                *OUT = MATCH_STRICT;
                return NORMAL;
        }

        if (strcmp(VALUE, "loose") == MATCH) {
                *OUT = MATCH_LOOSE;
                return NORMAL;
        }

        return ABNORMAL;
}

// @@ Parse checksum behavior documented by the current schema
static int gprofile_parse_checksum_mode(const char * VALUE, checksum_mode_t * OUT) {
        if (!VALUE || !OUT) {
                return ABNORMAL;
        }

        if (strcmp(VALUE, "auto") == MATCH) {
                *OUT = CHECKSUM_AUTO;
                return NORMAL;
        }

        if (strcmp(VALUE, "manual") == MATCH) {
                *OUT = CHECKSUM_MANUAL;
                return NORMAL;
        }

        return ABNORMAL;
}

// @@ Reset temporary profile storage to explicit invalid states
static void gprofile_reset(GLOBAL_PROFILE * GPROF) {
        if (!GPROF) {
                return;
        }

        memset(GPROF, 0x00, sizeof(GLOBAL_PROFILE));

        GPROF->profile_schema_version        = GLOBAL_RESET;

        GPROF->tx_strict                     = GLOBAL_RESET;
        GPROF->tx_checksum_mode              = CHECKSUM_UNSET;
        GPROF->tx_ipv4_id_generation         = TXG_UNSET;
        GPROF->tx_tcp_src_port_generation    = TXG_UNSET;
        GPROF->tx_ipv6_hop_limit             = GLOBAL_RESET;
        GPROF->tx_ipv4_ttl                   = GLOBAL_RESET;

        GPROF->rx_match_policy               = MATCH_UNSET;
        GPROF->rx_state                      = RXS_UNSET;
        GPROF->rx_promiscuous                = GLOBAL_RESET;
        GPROF->rx_timeout_ms                 = GLOBAL_RESET;
        GPROF->rx_snaplength                 = GLOBAL_RESET;
        GPROF->rx_dedupe                     = GLOBAL_RESET;

        GPROF->dns_timeout_ms                = GLOBAL_RESET;
        GPROF->dns_retries                   = GLOBAL_RESET;

        GPROF->limit_max_pending_transactions = (unsigned int)GLOBAL_RESET;
        GPROF->limit_max_capture_bytes_per_tx = (unsigned int)GLOBAL_RESET;
        GPROF->limit_max_dns_queries_pending  = (unsigned int)GLOBAL_RESET;
        GPROF->limit_max_targets_per_batch    = (unsigned int)GLOBAL_RESET;
        GPROF->limit_max_scan_workers         = 8U;
        GPROF->limit_max_tx_out_rate_pps      = (unsigned int)GLOBAL_RESET;
        GPROF->limit_max_rx_in_rate_pps       = (unsigned int)GLOBAL_RESET;
        GPROF->limit_max_capture_bytes_total  = (uint64_t)GLOBAL_RESET;
}

// @@ Find the schema definition for a tuple
static const GPROFILE_FIELD * gprofile_find_field(const GPROFILE_TUPLE * TUPLE) {
        c_size_t INDEX;

        if (!TUPLE || !TUPLE->KEY) {
                return NULL;
        }

        for (INDEX = 0; INDEX < GPROFILE_FIELD_COUNT; INDEX++) {
                if (
                        GPROFILE_FIELDS[INDEX].SECTION == TUPLE->SECTION
                        &&
                        strcmp(GPROFILE_FIELDS[INDEX].KEY, TUPLE->KEY) == MATCH
                ) {
                        return &GPROFILE_FIELDS[INDEX];
                }
        }

        return NULL;
}

// @@ Convert and store a tuple using its field schema
static int gprofile_apply_field(
        GLOBAL_PROFILE * GPROF,
        const GPROFILE_FIELD * FIELD,
        const char * VALUE
) {
        void * DST;
        int8_t ALLOW_EMPTY;

        if (!GPROF || !FIELD || !VALUE) {
                return ABNORMAL;
        }

        DST = (unsigned char *)GPROF + FIELD->OFFSET;
        ALLOW_EMPTY = (FIELD->FLAGS & GPROFILE_FIELD_ALLOW_EMPTY) ? ISTRUE : ISFALSE;

        switch (FIELD->TYPE) {
                case GPROFILE_VALUE_STRING:
                        return gprofile_copy_string(DST, FIELD->SIZE, VALUE, ALLOW_EMPTY);
                case GPROFILE_VALUE_BOOL:
                        return gprofile_parse_bool(VALUE, DST);
                case GPROFILE_VALUE_INT:
                        return gprofile_parse_int(VALUE, DST, FIELD->MIN, FIELD->MAX);
                case GPROFILE_VALUE_UINT:
                        return gprofile_parse_uint(VALUE, DST, FIELD->MAX);
                case GPROFILE_VALUE_U64:
                        return gprofile_parse_u64(VALUE, DST, FIELD->MAX);
                case GPROFILE_VALUE_IP:
                        return gprofile_copy_ip(DST, FIELD->SIZE, VALUE, ALLOW_EMPTY);
                case GPROFILE_VALUE_IPV4:
                        return gprofile_copy_ipv4(DST, FIELD->SIZE, VALUE, ALLOW_EMPTY);
                case GPROFILE_VALUE_INTERFACE:
                        return gprofile_copy_interface(DST, FIELD->SIZE, VALUE, FIELD->SPECIAL_VALUE);
                case GPROFILE_VALUE_TX_GENERATION:
                        return gprofile_parse_tx_generation(VALUE, DST);
                case GPROFILE_VALUE_RX_STATE:
                        return gprofile_parse_rx_state(VALUE, DST);
                case GPROFILE_VALUE_MATCH_POLICY:
                        return gprofile_parse_match_policy(VALUE, DST);
                case GPROFILE_VALUE_CHECKSUM_MODE:
                        return gprofile_parse_checksum_mode(VALUE, DST);
                default:
                        return ABNORMAL;
        }
}

// @@ Analyze a tuple for existence, duplication, type, and range
static int gprofile_analyze_tuple(
        GLOBAL_PROFILE * GPROF,
        GPROFILE_ANALYSIS * ANALYSIS,
        const GPROFILE_TUPLE * TUPLE,
        const char ** ERROR_MESSAGE
) {
        const GPROFILE_FIELD * FIELD;

        if (!GPROF || !ANALYSIS || !TUPLE || !ERROR_MESSAGE) {
                return ABNORMAL;
        }

        FIELD = gprofile_find_field(TUPLE);

        if (!FIELD) {
                *ERROR_MESSAGE = "Unknown field.";
                return ABNORMAL;
        }

        if ((ANALYSIS->FIELDS & FIELD->SEEN_FLAG) != 0) {
                *ERROR_MESSAGE = "Duplicate field.";
                return ABNORMAL;
        }

        if (gprofile_apply_field(GPROF, FIELD, TUPLE->VALUE) == ABNORMAL) {
                *ERROR_MESSAGE = "Invalid field value.";
                return ABNORMAL;
        }

        ANALYSIS->FIELDS |= FIELD->SEEN_FLAG;
        return NORMAL;
}

// @@ Verify every schema field appeared exactly once
static int gprofile_validate_completeness(const GPROFILE_ANALYSIS * ANALYSIS, const char ** ERROR_MESSAGE) {
        c_size_t INDEX;

        if (!ANALYSIS || !ERROR_MESSAGE) {
                return ABNORMAL;
        }

        if (ANALYSIS->SECTIONS != GPROFILE_SECTION_REQUIRED) {
                *ERROR_MESSAGE = "One or more required sections are missing.";
                return ABNORMAL;
        }

        for (INDEX = 0; INDEX < GPROFILE_FIELD_COUNT; INDEX++) {
                if (GPROFILE_FIELDS[INDEX].FLAGS & GPROFILE_FIELD_OPTIONAL) {
                        continue;
                }

                if ((ANALYSIS->FIELDS & GPROFILE_FIELDS[INDEX].SEEN_FLAG) == 0) {
                        *ERROR_MESSAGE = "One or more required fields are missing.";
                        return ABNORMAL;
                }
        }

        return NORMAL;
}

// @@ Identify the address family of a validated profile IP string
static int gprofile_ip_family(const unsigned char * VALUE) {
        struct in_addr ADDRESS4;
        struct in6_addr ADDRESS6;

        if (!VALUE || VALUE[0] == 0x00) {
                return AF_UNSPEC;
        }

        if (inet_pton(AF_INET, (const char *)VALUE, &ADDRESS4) == 1) {
                return AF_INET;
        }

        if (inet_pton(AF_INET6, (const char *)VALUE, &ADDRESS6) == 1) {
                return AF_INET6;
        }

        return ABNORMAL;
}

// @@ Verify relationships that cannot be checked from one tuple alone
static int gprofile_validate_semantics(const GLOBAL_PROFILE * GPROF, const char ** ERROR_MESSAGE) {
        if (!GPROF || !ERROR_MESSAGE) {
                return ABNORMAL;
        }

        if (GPROF->profile_schema_version != 1) {
                *ERROR_MESSAGE = "Unsupported global profile schema version.";
                return ABNORMAL;
        }

        if (strcmp((const char *)GPROF->scope, "global") != MATCH) {
                *ERROR_MESSAGE = "Global profile scope must be 'global'.";
                return ABNORMAL;
        }

        if (
                GPROF->tx_source_ip[0] != 0x00
                && GPROF->tx_gateway[0] != 0x00
                && gprofile_ip_family(GPROF->tx_source_ip) != gprofile_ip_family(GPROF->tx_gateway)
        ) {
                *ERROR_MESSAGE = "Transmit source_ip and gateway must use the same address family.";
                return ABNORMAL;
        }

        if (GPROF->dns_server1[0] == 0x00) {
                *ERROR_MESSAGE = "DNS server1 is required.";
                return ABNORMAL;
        }

        if (GPROF->limit_max_capture_bytes_per_tx > GPROF->limit_max_capture_bytes_total) {
                *ERROR_MESSAGE = "Per-transaction capture limit exceeds total capture storage.";
                return ABNORMAL;
        }

        if (GPROF->limit_max_dns_queries_pending > GPROF->limit_max_pending_transactions) {
                *ERROR_MESSAGE = "Pending DNS query limit exceeds total pending transactions.";
                return ABNORMAL;
        }

        return NORMAL;
}

// @@ Convert a section token to the parser enumeration
static int gprofile_section_from_string(const char * NAME, gprofile_section_t * OUT) {
        if (!NAME || !OUT) {
                return ABNORMAL;
        }

        if (strcmp(NAME, "profile") == MATCH) {
                *OUT = GPROFILE_SECTION_TYPE_PROFILE;
                return NORMAL;
        }

        if (strcmp(NAME, "tx") == MATCH) {
                *OUT = GPROFILE_SECTION_TYPE_TX;
                return NORMAL;
        }

        if (strcmp(NAME, "rx") == MATCH) {
                *OUT = GPROFILE_SECTION_TYPE_RX;
                return NORMAL;
        }

        if (strcmp(NAME, "dns") == MATCH) {
                *OUT = GPROFILE_SECTION_TYPE_DNS;
                return NORMAL;
        }

        if (strcmp(NAME, "limits") == MATCH) {
                *OUT = GPROFILE_SECTION_TYPE_LIMITS;
                return NORMAL;
        }

        return ABNORMAL;
}

// @@ Normalize a user-supplied profile name into a safe basename and filename
static int gprofile_normalize_name(
        const unsigned char * PROFILE_NAME,
        char * BASENAME,
        c_size_t BASENAME_SIZE,
        char * FILENAME,
        c_size_t FILENAME_SIZE
) {
        const char * INPUT;
        c_size_t LENGTH;
        c_size_t BASE_LENGTH;
        c_size_t INDEX;

        if (
                !PROFILE_NAME
                || !BASENAME
                || BASENAME_SIZE == 0
                || !FILENAME
                || FILENAME_SIZE == 0
        ) {
                gprofile_set_error("Invalid profile name arguments.");
                return ABNORMAL;
        }

        INPUT = (const char *)PROFILE_NAME;
        LENGTH = strnlen(INPUT, PROFILE_NAME_BLOCK + 5);

        if (LENGTH == 0 || LENGTH >= PROFILE_NAME_BLOCK + 4) {
                gprofile_set_error(
                        "Profile names must contain between 1 and %d characters before the .ini suffix.",
                        PROFILE_NAME_BLOCK - 1
                );
                return ABNORMAL;
        }

        BASE_LENGTH = LENGTH;

        if (LENGTH > 4 && strcmp(INPUT + LENGTH - 4, ".ini") == MATCH) {
                BASE_LENGTH -= 4;
        }

        if (BASE_LENGTH == 0 || BASE_LENGTH >= BASENAME_SIZE || BASE_LENGTH >= PROFILE_NAME_BLOCK) {
                gprofile_set_error(
                        "Profile names must contain between 1 and %d characters before the .ini suffix.",
                        PROFILE_NAME_BLOCK - 1
                );
                return ABNORMAL;
        }

        for (INDEX = 0; INDEX < BASE_LENGTH; INDEX++) {
                unsigned char CHARACTER = (unsigned char)INPUT[INDEX];

                if (
                        !isalnum(CHARACTER)
                        && CHARACTER != '_'
                        && CHARACTER != '-'
                ) {
                        gprofile_set_error(
                                "Profile names may only contain letters, numbers, underscores, and hyphens."
                        );
                        return ABNORMAL;
                }
        }

        memset(BASENAME, 0x00, BASENAME_SIZE);
        memcpy(BASENAME, INPUT, BASE_LENGTH);

        if (snprintf(FILENAME, FILENAME_SIZE, "%s.ini", BASENAME) >= (int)FILENAME_SIZE) {
                gprofile_set_error("Profile filename exceeds the path buffer.");
                return ABNORMAL;
        }

        return NORMAL;
}

// @@ Build a path that cannot escape the user profile directory
static int gprofile_build_user_path(
        const unsigned char * PROFILE_NAME,
        char * PATH,
        c_size_t PATH_SIZE,
        char * BASENAME,
        c_size_t BASENAME_SIZE
) {
        char FILENAME[PROFILE_NAME_BLOCK + 4];
        int WRITTEN;

        if (!PATH || PATH_SIZE == 0) {
                gprofile_set_error("Invalid profile path buffer.");
                return ABNORMAL;
        }

        memset(FILENAME, 0x00, sizeof(FILENAME));

        if (
                gprofile_normalize_name(
                        PROFILE_NAME,
                        BASENAME,
                        BASENAME_SIZE,
                        FILENAME,
                        sizeof(FILENAME)
                ) == ABNORMAL
        ) {
                return ABNORMAL;
        }

        WRITTEN = snprintf(PATH, PATH_SIZE, "%s%s", KAMI_USER_PROFILES_DIR, FILENAME);

        if (WRITTEN < 0 || (c_size_t)WRITTEN >= PATH_SIZE) {
                gprofile_set_error("Profile path exceeds the path buffer.");
                return ABNORMAL;
        }

        return NORMAL;
}

// @@ Parse a complete profile into temporary storage
static int gprofile_parse_file(const char * PATH, GLOBAL_PROFILE * OUT) {
        FILE * PROFILE_FILE;
        GLOBAL_PROFILE TMP_GPROF;
        GPROFILE_ANALYSIS ANALYSIS;
        GPROFILE_TUPLE TUPLE;
        gprofile_section_t CURRENT_SECTION;
        struct stat INFO;
        char LINE[GPROFILE_LINE_BLOCK];
        char * WORK;
        const char * ERROR_MESSAGE;
        unsigned long LINE_NUMBER;
        int PROFILE_FD;

        if (!PATH || !OUT) {
                gprofile_set_error("Invalid parser arguments.");
                return ABNORMAL;
        }

        PROFILE_FD = open(
                PATH,
                O_RDONLY
#ifdef O_NOFOLLOW
                | O_NOFOLLOW
#endif
        );

        if (PROFILE_FD < 0) {
                gprofile_set_error("Failed to open profile: %s", strerror(errno));
                return ABNORMAL;
        }

        memset(&INFO, 0x00, sizeof(INFO));

        if (fstat(PROFILE_FD, &INFO) != NORMAL || !S_ISREG(INFO.st_mode)) {
                close(PROFILE_FD);
                gprofile_set_error("Profile path is not a normal file.");
                return ABNORMAL;
        }

        PROFILE_FILE = fdopen(PROFILE_FD, "rb");

        if (!PROFILE_FILE) {
                close(PROFILE_FD);
                gprofile_set_error("Failed to create a profile input stream: %s", strerror(errno));
                return ABNORMAL;
        }

        gprofile_reset(&TMP_GPROF);
        memset(&ANALYSIS, 0x00, sizeof(ANALYSIS));
        memset(&TUPLE, 0x00, sizeof(TUPLE));
        memset(LINE, 0x00, sizeof(LINE));

        CURRENT_SECTION = GPROFILE_SECTION_UNSET;
        LINE_NUMBER = 0;

        while (fgets(LINE, sizeof(LINE), PROFILE_FILE) != NULL) {
                LINE_NUMBER++;

                if (gprofile_validate_line_length(PROFILE_FILE, LINE) == ABNORMAL) {
                        gprofile_set_error(
                                "Profile line %lu exceeds %d bytes.",
                                LINE_NUMBER,
                                GPROFILE_LINE_BLOCK - 1
                        );

                        fclose(PROFILE_FILE);
                        return ABNORMAL;
                }

                WORK = gprofile_prepare_line(LINE);

                if (!WORK || WORK[0] == 0x00) {
                        memset(LINE, 0x00, sizeof(LINE));
                        continue;
                }

                if (WORK[0] == '[') {
                        if (gprofile_extract_section(WORK, &CURRENT_SECTION, &ANALYSIS) == ABNORMAL) {
                                gprofile_set_error(
                                        "Invalid, unknown, or duplicate profile section on line %lu.",
                                        LINE_NUMBER
                                );

                                fclose(PROFILE_FILE);
                                return ABNORMAL;
                        }

                        memset(LINE, 0x00, sizeof(LINE));
                        continue;
                }

                if (CURRENT_SECTION == GPROFILE_SECTION_UNSET) {
                        gprofile_set_error(
                                "Profile key without a section on line %lu.",
                                LINE_NUMBER
                        );

                        fclose(PROFILE_FILE);
                        return ABNORMAL;
                }

                if (gprofile_extract_tuple(WORK, CURRENT_SECTION, LINE_NUMBER, &TUPLE) == ABNORMAL) {
                        gprofile_set_error(
                                "Invalid profile key/value tuple on line %lu.",
                                LINE_NUMBER
                        );

                        fclose(PROFILE_FILE);
                        return ABNORMAL;
                }

                ERROR_MESSAGE = NULL;

                if (gprofile_analyze_tuple(&TMP_GPROF, &ANALYSIS, &TUPLE, &ERROR_MESSAGE) == ABNORMAL) {
                        gprofile_set_error(
                                "%s Line %lu: [%s] %s = %s",
                                ERROR_MESSAGE ? ERROR_MESSAGE : "Profile tuple analysis failed.",
                                TUPLE.LINE_NUMBER,
                                gprofile_section_name(TUPLE.SECTION),
                                TUPLE.KEY,
                                TUPLE.VALUE
                        );

                        fclose(PROFILE_FILE);
                        return ABNORMAL;
                }

                memset(&TUPLE, 0x00, sizeof(TUPLE));
                memset(LINE, 0x00, sizeof(LINE));
        }

        if (ferror(PROFILE_FILE)) {
                gprofile_set_error("Failure while reading profile.");
                fclose(PROFILE_FILE);
                return ABNORMAL;
        }

        fclose(PROFILE_FILE);
        ERROR_MESSAGE = NULL;

        if (gprofile_validate_completeness(&ANALYSIS, &ERROR_MESSAGE) == ABNORMAL) {
                gprofile_set_error(
                        "Profile failed validation: %s",
                        ERROR_MESSAGE ? ERROR_MESSAGE : "Incomplete profile."
                );
                return ABNORMAL;
        }

        if (gprofile_validate_semantics(&TMP_GPROF, &ERROR_MESSAGE) == ABNORMAL) {
                gprofile_set_error(
                        "Profile failed validation: %s",
                        ERROR_MESSAGE ? ERROR_MESSAGE : "Invalid profile relationships."
                );
                return ABNORMAL;
        }

        *OUT = TMP_GPROF;
        return NORMAL;
}

// @@ Resolve section.key or a unique bare key into a field schema
static const GPROFILE_FIELD * gprofile_resolve_runtime_field(const unsigned char * KEY) {
        const GPROFILE_FIELD * MATCHED_FIELD;
        gprofile_section_t SECTION;
        c_size_t INDEX;
        c_size_t MATCH_COUNT;
        char KEY_BUFFER[GPROFILE_KEY_BLOCK];
        char * FIELD_NAME;
        char * SEPARATOR;

        if (!KEY || KEY[0] == 0x00) {
                gprofile_set_error("Profile field key cannot be empty.");
                return NULL;
        }

        if (strnlen((const char *)KEY, sizeof(KEY_BUFFER)) >= sizeof(KEY_BUFFER)) {
                gprofile_set_error("Profile field key is too long.");
                return NULL;
        }

        memset(KEY_BUFFER, 0x00, sizeof(KEY_BUFFER));
        snprintf(KEY_BUFFER, sizeof(KEY_BUFFER), "%s", (const char *)KEY);
        SEPARATOR = strchr(KEY_BUFFER, '.');

        if (SEPARATOR) {
                if (strchr(SEPARATOR + 1, '.')) {
                        gprofile_set_error("Profile fields use one section.key separator.");
                        return NULL;
                }

                *SEPARATOR = 0x00;
                FIELD_NAME = SEPARATOR + 1;

                if (
                        KEY_BUFFER[0] == 0x00
                        || FIELD_NAME[0] == 0x00
                        || gprofile_section_from_string(KEY_BUFFER, &SECTION) == ABNORMAL
                ) {
                        gprofile_set_error("Unknown profile section in field key.");
                        return NULL;
                }

                for (INDEX = 0; INDEX < GPROFILE_FIELD_COUNT; INDEX++) {
                        if (GPROFILE_FIELDS[INDEX].FLAGS & GPROFILE_FIELD_LEGACY) {
                                continue;
                        }

                        if (
                                GPROFILE_FIELDS[INDEX].SECTION == SECTION
                                && strcmp(GPROFILE_FIELDS[INDEX].KEY, FIELD_NAME) == MATCH
                        ) {
                                return &GPROFILE_FIELDS[INDEX];
                        }
                }

                gprofile_set_error("Unknown profile field: %s.%s", KEY_BUFFER, FIELD_NAME);
                return NULL;
        }

        MATCHED_FIELD = NULL;
        MATCH_COUNT = 0;

        for (INDEX = 0; INDEX < GPROFILE_FIELD_COUNT; INDEX++) {
                if (GPROFILE_FIELDS[INDEX].FLAGS & GPROFILE_FIELD_LEGACY) {
                        continue;
                }

                if (strcmp(GPROFILE_FIELDS[INDEX].KEY, KEY_BUFFER) == MATCH) {
                        MATCHED_FIELD = &GPROFILE_FIELDS[INDEX];
                        MATCH_COUNT++;
                }
        }

        if (MATCH_COUNT == 0) {
                gprofile_set_error("Unknown profile field: %s", KEY_BUFFER);
                return NULL;
        }

        if (MATCH_COUNT > 1) {
                gprofile_set_error(
                        "Profile field '%s' is ambiguous; use section.%s.",
                        KEY_BUFFER,
                        KEY_BUFFER
                );
                return NULL;
        }

        return MATCHED_FIELD;
}

// @@ Convert runtime enum values into canonical INI strings
static const char * gprofile_bool_string(int8_t VALUE) {
        if (VALUE == ISTRUE) {
                return "true";
        }

        if (VALUE == ISFALSE) {
                return "false";
        }

        return NULL;
}

static const char * gprofile_tx_generation_string(tx_generation_t VALUE) {
        switch (VALUE) {
                case TXG_RANDOM:    return "random";
                case TXG_STATIC:    return "static";
                case TXG_INCREMENT: return "increment";
                default:            return NULL;
        }
}

static const char * gprofile_rx_state_string(rx_state_t VALUE) {
        switch (VALUE) {
                case RXS_TRANSACTION: return "transaction";
                case RXS_STREAM:      return "stream";
                default:              return NULL;
        }
}

static const char * gprofile_match_policy_string(match_policy_t VALUE) {
        switch (VALUE) {
                case MATCH_STRICT: return "strict";
                case MATCH_LOOSE:  return "loose";
                default:           return NULL;
        }
}

static const char * gprofile_checksum_mode_string(checksum_mode_t VALUE) {
        switch (VALUE) {
                case CHECKSUM_AUTO:   return "auto";
                case CHECKSUM_MANUAL: return "manual";
                default:              return NULL;
        }
}

// @@ Write the active schema in one canonical INI structure
static int gprofile_write_stream(FILE * PROFILE_FILE, const GLOBAL_PROFILE * GPROF) {
        const char * TX_CHECKSUM_MODE;
        const char * TX_IPV4_ID_GENERATION;
        const char * TX_TCP_SRC_PORT_GENERATION;
        const char * TX_STRICT;
        const char * RX_MATCH_POLICY;
        const char * RX_STATE;
        const char * RX_PROMISCUOUS;
        const char * RX_DEDUPE;

        if (!PROFILE_FILE || !GPROF) {
                gprofile_set_error("Invalid profile serialization arguments.");
                return ABNORMAL;
        }

        TX_CHECKSUM_MODE = gprofile_checksum_mode_string(GPROF->tx_checksum_mode);
        TX_IPV4_ID_GENERATION = gprofile_tx_generation_string(GPROF->tx_ipv4_id_generation);
        TX_TCP_SRC_PORT_GENERATION = gprofile_tx_generation_string(GPROF->tx_tcp_src_port_generation);
        TX_STRICT = gprofile_bool_string(GPROF->tx_strict);
        RX_MATCH_POLICY = gprofile_match_policy_string(GPROF->rx_match_policy);
        RX_STATE = gprofile_rx_state_string(GPROF->rx_state);
        RX_PROMISCUOUS = gprofile_bool_string(GPROF->rx_promiscuous);
        RX_DEDUPE = gprofile_bool_string(GPROF->rx_dedupe);

        if (
                !TX_CHECKSUM_MODE
                || !TX_IPV4_ID_GENERATION
                || !TX_TCP_SRC_PORT_GENERATION
                || !TX_STRICT
                || !RX_MATCH_POLICY
                || !RX_STATE
                || !RX_PROMISCUOUS
                || !RX_DEDUPE
        ) {
                gprofile_set_error("Runtime profile contains an unserializable value.");
                return ABNORMAL;
        }

        if (
                fprintf(
                        PROFILE_FILE,
                        "[profile]\n"
                        "name = %s\n"
                        "version = %d\n"
                        "scope = %s\n"
                        "\n"
                        "[tx]\n"
                        "interface = %s\n"
                        "source_ip = %s\n"
                        "gateway = %s\n"
                        "strict = %s\n"
                        "ttl = %d\n"
                        "hop_limit = %d\n"
                        "ip_id_mode = %s\n"
                        "tcp_src_port_mode = %s\n"
                        "checksum_mode = %s\n"
                        "\n"
                        "[rx]\n"
                        "interface = %s\n"
                        "timeout_ms = %d\n"
                        "promiscuous = %s\n"
                        "snaplength = %d\n"
                        "state = %s\n"
                        "dedupe = %s\n"
                        "match_policy = %s\n"
                        "\n"
                        "[dns]\n"
                        "server1 = %s\n"
                        "server2 = %s\n"
                        "search_domain = %s\n"
                        "timeout_ms = %d\n"
                        "retries = %d\n"
                        "\n"
                        "[limits]\n"
                        "max_pending_transactions = %u\n"
                        "max_capture_bytes_per_tx = %u\n"
                        "max_capture_bytes_total = %llu\n"
                        "max_tx_rate_pps = %u\n"
                        "max_rx_rate_pps = %u\n"
                        "max_dns_queries_pending = %u\n"
                        "max_targets_per_batch = %u\n"
                        "max_scan_workers = %u\n",
                        GPROF->profile_name,
                        GPROF->profile_schema_version,
                        GPROF->scope,
                        GPROF->tx_interface,
                        GPROF->tx_source_ip,
                        GPROF->tx_gateway,
                        TX_STRICT,
                        GPROF->tx_ipv4_ttl,
                        GPROF->tx_ipv6_hop_limit,
                        TX_IPV4_ID_GENERATION,
                        TX_TCP_SRC_PORT_GENERATION,
                        TX_CHECKSUM_MODE,
                        GPROF->rx_interface,
                        GPROF->rx_timeout_ms,
                        RX_PROMISCUOUS,
                        GPROF->rx_snaplength,
                        RX_STATE,
                        RX_DEDUPE,
                        RX_MATCH_POLICY,
                        GPROF->dns_server1,
                        GPROF->dns_server2,
                        GPROF->dns_search_domain,
                        GPROF->dns_timeout_ms,
                        GPROF->dns_retries,
                        GPROF->limit_max_pending_transactions,
                        GPROF->limit_max_capture_bytes_per_tx,
                        (unsigned long long)GPROF->limit_max_capture_bytes_total,
                        GPROF->limit_max_tx_out_rate_pps,
                        GPROF->limit_max_rx_in_rate_pps,
                        GPROF->limit_max_dns_queries_pending,
                        GPROF->limit_max_targets_per_batch,
                        GPROF->limit_max_scan_workers
                ) < 0
        ) {
                gprofile_set_error("Failed while serializing the profile.");
                return ABNORMAL;
        }

        return NORMAL;
}

// @@ Load a named global profile without altering runtime data on partial failure
int load_global_profile(
        const unsigned char * PROFILE_NAME,
        _carry_forward * _prog_data
) {
        GLOBAL_PROFILE TMP_GPROF;
        char PROFILE_PATH[MAX_PATH];
        char PROFILE_BASENAME[PROFILE_NAME_BLOCK];

        memset(GPROFILE_ERROR, 0x00, sizeof(GPROFILE_ERROR));
        memset(PROFILE_PATH, 0x00, sizeof(PROFILE_PATH));
        memset(PROFILE_BASENAME, 0x00, sizeof(PROFILE_BASENAME));

        if (!_prog_data) {
                gprofile_set_error("Runtime data is unavailable.");
                return ABNORMAL;
        }

        if (
                gprofile_build_user_path(
                        PROFILE_NAME,
                        PROFILE_PATH,
                        sizeof(PROFILE_PATH),
                        PROFILE_BASENAME,
                        sizeof(PROFILE_BASENAME)
                ) == ABNORMAL
        ) {
                return ABNORMAL;
        }

        if (validate_regular_file_presence(PROFILE_PATH) == ABNORMAL) {
                if (strcmp(PROFILE_BASENAME, "default") != MATCH) {
                        gprofile_set_error("Profile '%s.ini' was not found.", PROFILE_BASENAME);
                        return ABNORMAL;
                }

                if (validate_regular_file_presence(KAMI_SYSTEM_DEFAULT_PROFILE) == ABNORMAL) {
                        gprofile_set_error("No user default profile or system backup was found.");
                        return ABNORMAL;
                }

                if (copy_regular_file(KAMI_SYSTEM_DEFAULT_PROFILE, PROFILE_PATH) == ABNORMAL) {
                        gprofile_set_error("Failed to restore the generic default profile.");
                        return ABNORMAL;
                }
        }

        if (gprofile_parse_file(PROFILE_PATH, &TMP_GPROF) == ABNORMAL) {
                return ABNORMAL;
        }

        if (strcmp((const char *)TMP_GPROF.profile_name, PROFILE_BASENAME) != MATCH) {
                gprofile_set_error(
                        "Profile metadata name '%s' does not match filename '%s.ini'.",
                        TMP_GPROF.profile_name,
                        PROFILE_BASENAME
                );
                return ABNORMAL;
        }

        _prog_data->gprof = TMP_GPROF;
        return NORMAL;
}

// @@ Validate and commit one runtime field without partial mutation
int gprofile_set_runtime_value(
        const unsigned char * KEY,
        const unsigned char * VALUE,
        _carry_forward * _prog_data
) {
        GLOBAL_PROFILE TMP_GPROF;
        const GPROFILE_FIELD * FIELD;
        const char * ERROR_MESSAGE;

        memset(GPROFILE_ERROR, 0x00, sizeof(GPROFILE_ERROR));

        if (!_prog_data || !KEY || !VALUE) {
                gprofile_set_error("Invalid runtime profile update arguments.");
                return ABNORMAL;
        }

        FIELD = gprofile_resolve_runtime_field(KEY);

        if (!FIELD) {
                return ABNORMAL;
        }

        TMP_GPROF = _prog_data->gprof;

        if (gprofile_apply_field(&TMP_GPROF, FIELD, (const char *)VALUE) == ABNORMAL) {
                gprofile_set_error(
                        "Invalid value '%s' for [%s] %s.",
                        VALUE,
                        gprofile_section_name(FIELD->SECTION),
                        FIELD->KEY
                );
                return ABNORMAL;
        }

        ERROR_MESSAGE = NULL;

        if (gprofile_validate_semantics(&TMP_GPROF, &ERROR_MESSAGE) == ABNORMAL) {
                gprofile_set_error(
                        "Runtime profile update rejected: %s",
                        ERROR_MESSAGE ? ERROR_MESSAGE : "Invalid profile relationships."
                );
                return ABNORMAL;
        }

        _prog_data->gprof = TMP_GPROF;
        return NORMAL;
}

// @@ Save the active runtime profile using an atomic same-directory commit
int gprofile_save_runtime(
        const unsigned char * PROFILE_NAME,
        int8_t ALLOW_OVERWRITE,
        _carry_forward * _prog_data
) {
        GLOBAL_PROFILE TMP_GPROF;
        FILE * PROFILE_FILE;
        const char * ERROR_MESSAGE;
        char PROFILE_PATH[MAX_PATH];
        char PROFILE_BASENAME[PROFILE_NAME_BLOCK];
        char TEMP_PATH[MAX_PATH];
        int PROFILE_FD;
        int RESULT;

        memset(GPROFILE_ERROR, 0x00, sizeof(GPROFILE_ERROR));
        memset(PROFILE_PATH, 0x00, sizeof(PROFILE_PATH));
        memset(PROFILE_BASENAME, 0x00, sizeof(PROFILE_BASENAME));
        memset(TEMP_PATH, 0x00, sizeof(TEMP_PATH));

        if (!_prog_data) {
                gprofile_set_error("Runtime data is unavailable.");
                return ABNORMAL;
        }

        if (
                gprofile_build_user_path(
                        PROFILE_NAME,
                        PROFILE_PATH,
                        sizeof(PROFILE_PATH),
                        PROFILE_BASENAME,
                        sizeof(PROFILE_BASENAME)
                ) == ABNORMAL
        ) {
                return ABNORMAL;
        }

        if (validate_regular_file_presence(PROFILE_PATH) == NORMAL && ALLOW_OVERWRITE != ISTRUE) {
                gprofile_set_error("Profile '%s.ini' already exists.", PROFILE_BASENAME);
                return GPROFILE_SAVE_EXISTS;
        }

        TMP_GPROF = _prog_data->gprof;

        if (
                gprofile_copy_string(
                        TMP_GPROF.profile_name,
                        sizeof(TMP_GPROF.profile_name),
                        PROFILE_BASENAME,
                        ISFALSE
                ) == ABNORMAL
        ) {
                gprofile_set_error("Failed to update profile metadata for the save operation.");
                return ABNORMAL;
        }

        ERROR_MESSAGE = NULL;

        if (gprofile_validate_semantics(&TMP_GPROF, &ERROR_MESSAGE) == ABNORMAL) {
                gprofile_set_error(
                        "Runtime profile cannot be saved: %s",
                        ERROR_MESSAGE ? ERROR_MESSAGE : "Invalid profile relationships."
                );
                return ABNORMAL;
        }

        if (
                snprintf(
                        TEMP_PATH,
                        sizeof(TEMP_PATH),
                        "%s.%s.tmp.XXXXXX",
                        KAMI_USER_PROFILES_DIR,
                        PROFILE_BASENAME
                ) >= (int)sizeof(TEMP_PATH)
        ) {
                gprofile_set_error("Temporary profile path exceeds the path buffer.");
                return ABNORMAL;
        }

        PROFILE_FD = mkstemp(TEMP_PATH);

        if (PROFILE_FD < 0) {
                gprofile_set_error("Failed to create a temporary profile: %s", strerror(errno));
                return ABNORMAL;
        }

        PROFILE_FILE = fdopen(PROFILE_FD, "wb");

        if (!PROFILE_FILE) {
                close(PROFILE_FD);
                remove(TEMP_PATH);
                gprofile_set_error("Failed to create a profile output stream: %s", strerror(errno));
                return ABNORMAL;
        }

        RESULT = gprofile_write_stream(PROFILE_FILE, &TMP_GPROF);

        if (RESULT == NORMAL && fflush(PROFILE_FILE) != NORMAL) {
                gprofile_set_error("Failed to flush the temporary profile: %s", strerror(errno));
                RESULT = ABNORMAL;
        }

        if (RESULT == NORMAL && fsync(fileno(PROFILE_FILE)) != NORMAL) {
                gprofile_set_error("Failed to synchronize the temporary profile: %s", strerror(errno));
                RESULT = ABNORMAL;
        }

        if (fclose(PROFILE_FILE) != NORMAL) {
                if (RESULT == NORMAL) {
                        gprofile_set_error("Failed to close the temporary profile: %s", strerror(errno));
                }
                RESULT = ABNORMAL;
        }

        if (RESULT == ABNORMAL) {
                remove(TEMP_PATH);
                return ABNORMAL;
        }

        if (ALLOW_OVERWRITE == ISTRUE) {
                if (rename(TEMP_PATH, PROFILE_PATH) != NORMAL) {
                        gprofile_set_error("Failed to replace profile: %s", strerror(errno));
                        remove(TEMP_PATH);
                        return ABNORMAL;
                }
        } else {
                if (link(TEMP_PATH, PROFILE_PATH) != NORMAL) {
                        if (errno == EEXIST) {
                                gprofile_set_error("Profile '%s.ini' already exists.", PROFILE_BASENAME);
                                remove(TEMP_PATH);
                                return GPROFILE_SAVE_EXISTS;
                        }

                        gprofile_set_error("Failed to commit profile: %s", strerror(errno));
                        remove(TEMP_PATH);
                        return ABNORMAL;
                }

                if (remove(TEMP_PATH) != NORMAL) {
                        gprofile_set_error("Profile saved, but temporary file cleanup failed: %s", strerror(errno));
                        return ABNORMAL;
                }
        }

        _prog_data->gprof = TMP_GPROF;
        return NORMAL;
}
