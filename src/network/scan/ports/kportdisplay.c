// Copyright 2026 Jamison A. Drapeau
#include "kportdisplay.h"
#include "kportscan.h"
#include "kportselect.h"
#include "kbanner.h"
#include "kscan.h"
#include "kui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KPORTDISPLAY_PROTOCOLS       2U
#define KPORTDISPLAY_FAMILIES        2U
#define KPORTDISPLAY_TABLE_LIMIT   512U
#define KPORTDISPLAY_DETAIL_LIMIT  128U

typedef struct KPORTDISPLAY_SLOT {
        uint64_t TIMESTAMP_NS;
        uint32_t RX_BYTES;
        uint8_t PRESENT;
        uint8_t STATE;
        uint8_t EVIDENCE;
} KPORTDISPLAY_SLOT;

static size_t kportdisplay_index(
        unsigned int PROTOCOL_INDEX,
        unsigned int FAMILY_INDEX,
        unsigned int PORT
) {
        return (
                ((size_t)PROTOCOL_INDEX * KPORTDISPLAY_FAMILIES)
                + (size_t)FAMILY_INDEX
        ) * MAX_PORTS + (size_t)PORT;
}

static int kportdisplay_protocol_index(const char * PROTOCOL) {
        if (!PROTOCOL) {
                return -1;
        }
        if (strcmp(PROTOCOL, "TCP") == MATCH) {
                return 0;
        }
        if (strcmp(PROTOCOL, "UDP") == MATCH) {
                return 1;
        }
        return -1;
}

static int kportdisplay_family_index(unsigned int FAMILY) {
        if (FAMILY == 4U) {
                return 0;
        }
        if (FAMILY == 6U) {
                return 1;
        }
        return -1;
}

static KPORTSCAN_STATE kportdisplay_parse_state(const char * STATE) {
        if (!STATE) return KPORTSCAN_STATE_UNKNOWN;
        if (strcmp(STATE, "OPEN") == MATCH) return KPORTSCAN_STATE_OPEN;
        if (strcmp(STATE, "CLOSED") == MATCH) return KPORTSCAN_STATE_CLOSED;
        if (strcmp(STATE, "FILTERED") == MATCH) return KPORTSCAN_STATE_FILTERED;
        if (strcmp(STATE, "OPEN|FILTERED") == MATCH) return KPORTSCAN_STATE_OPEN_FILTERED;
        if (strcmp(STATE, "ERROR") == MATCH) return KPORTSCAN_STATE_ERROR;
        return KPORTSCAN_STATE_UNKNOWN;
}

static KPORTSCAN_EVIDENCE kportdisplay_parse_evidence(const char * EVIDENCE) {
        if (!EVIDENCE) return KPORTSCAN_EVIDENCE_NONE;
        if (strcmp(EVIDENCE, "SYN+ACK") == MATCH) return KPORTSCAN_EVIDENCE_TCP_SYN_ACK;
        if (strcmp(EVIDENCE, "RST") == MATCH) return KPORTSCAN_EVIDENCE_TCP_RST;
        if (strcmp(EVIDENCE, "UDP_REPLY") == MATCH) return KPORTSCAN_EVIDENCE_UDP_REPLY;
        if (strcmp(EVIDENCE, "ICMP_PORT_UNREACHABLE") == MATCH) return KPORTSCAN_EVIDENCE_ICMP_PORT_UNREACHABLE;
        if (strcmp(EVIDENCE, "ICMP_FILTERED") == MATCH) return KPORTSCAN_EVIDENCE_ICMP_FILTERED;
        if (strcmp(EVIDENCE, "TIMEOUT") == MATCH) return KPORTSCAN_EVIDENCE_TIMEOUT;
        if (strcmp(EVIDENCE, "NETWORK_ERROR") == MATCH) return KPORTSCAN_EVIDENCE_NETWORK_ERROR;
        return KPORTSCAN_EVIDENCE_NONE;
}

static const char * kportdisplay_state_string(uint8_t STATE) {
        return kportscan_state_string((KPORTSCAN_STATE)STATE);
}

static const char * kportdisplay_state_color(uint8_t STATE) {
        switch ((KPORTSCAN_STATE)STATE) {
                case KPORTSCAN_STATE_OPEN: return RGB_COLOR_BRIGHT_GREEN;
                case KPORTSCAN_STATE_CLOSED: return RGB_COLOR_SOFT_GREY;
                case KPORTSCAN_STATE_FILTERED: return RGB_COLOR_BRIGHT_YELLOW;
                case KPORTSCAN_STATE_OPEN_FILTERED: return RGB_COLOR_ORANGE;
                case KPORTSCAN_STATE_ERROR: return RGB_COLOR_RICH_RED;
                default: return RGB_COLOR_SOFT_GREY;
        }
}

static const char * kportdisplay_evidence_string(uint8_t EVIDENCE) {
        return kportscan_evidence_string((KPORTSCAN_EVIDENCE)EVIDENCE);
}

static const char * kportdisplay_protocol_string(unsigned int INDEX) {
        return INDEX == 0U ? "TCP" : "UDP";
}

static unsigned int kportdisplay_family_value(unsigned int INDEX) {
        return INDEX == 0U ? 4U : 6U;
}

static int kportdisplay_slot_priority(const KPORTDISPLAY_SLOT * SLOT) {
        if (!SLOT || SLOT->PRESENT != ISTRUE) {
                return 99;
        }

        if (
                SLOT->STATE == KPORTSCAN_STATE_OPEN
                || SLOT->STATE == KPORTSCAN_STATE_ERROR
        ) {
                return 0;
        }

        if (
                SLOT->STATE == KPORTSCAN_STATE_FILTERED
                || SLOT->STATE == KPORTSCAN_STATE_OPEN_FILTERED
        ) {
                return 1;
        }

        return 2;
}

static void kportdisplay_print_row(
        unsigned int PROTOCOL_INDEX,
        unsigned int FAMILY_INDEX,
        unsigned int PORT,
        const KPORTDISPLAY_SLOT * SLOT
) {
        if (!SLOT) {
                return;
        }

        kui_add_line(
                " %-5s %6u %4u     %s%-15s%s %s",
                kportdisplay_protocol_string(PROTOCOL_INDEX),
                PORT,
                kportdisplay_family_value(FAMILY_INDEX),
                kportdisplay_state_color(SLOT->STATE),
                kportdisplay_state_string(SLOT->STATE),
                ANSI_COLOR_RESET,
                kportdisplay_evidence_string(SLOT->EVIDENCE)
        );
}

static void kportdisplay_print_detail(
        unsigned int PROTOCOL_INDEX,
        unsigned int FAMILY_INDEX,
        unsigned int PORT,
        const KPORTDISPLAY_SLOT * SLOT
) {
        char LAST_SEEN[64];

        if (!SLOT) {
                return;
        }

        memset(LAST_SEEN, 0x00, sizeof(LAST_SEEN));
        kscan_format_ns(
                SLOT->TIMESTAMP_NS,
                LAST_SEEN,
                sizeof(LAST_SEEN)
        );

        kui_add_line("");
        kui_add_line(
                ANSI_COLOR_CYAN
                " PORT %u/%s:"
                ANSI_COLOR_RESET,
                PORT,
                kportdisplay_protocol_string(PROTOCOL_INDEX)
        );
        kui_add_line(
                RGB_COLOR_SOFT_GREY
                " ─────────────────────────────────────────────────────────"
                ANSI_COLOR_RESET
        );
        kui_add_line(
                " STATE:      %s%s%s",
                kportdisplay_state_color(SLOT->STATE),
                kportdisplay_state_string(SLOT->STATE),
                ANSI_COLOR_RESET
        );
        kui_add_line(
                " RETURN:     %s",
                kportdisplay_evidence_string(SLOT->EVIDENCE)
        );
        kui_add_line(
                " AF:         IPv%u",
                kportdisplay_family_value(FAMILY_INDEX)
        );
        kui_add_line(
                " LAST SEEN:  %s",
                LAST_SEEN
        );
        kui_add_line(
                " RX BYTES:   %u",
                SLOT->RX_BYTES
        );

}

static void kportdisplay_print_sorted_table(
        const KPORTDISPLAY_SLOT * SLOTS,
        uint64_t TOTAL,
        uint64_t * PRINTED
) {
        if (!SLOTS || !PRINTED) {
                return;
        }

        if (TOTAL <= KPORTDISPLAY_TABLE_LIMIT) {
                for (unsigned int PROTOCOL = 0; PROTOCOL < KPORTDISPLAY_PROTOCOLS; PROTOCOL++) {
                        for (unsigned int PORT = 1; PORT < MAX_PORTS; PORT++) {
                                for (unsigned int FAMILY = 0; FAMILY < KPORTDISPLAY_FAMILIES; FAMILY++) {
                                        const KPORTDISPLAY_SLOT * SLOT = &SLOTS[
                                                kportdisplay_index(PROTOCOL, FAMILY, PORT)
                                        ];

                                        if (SLOT->PRESENT != ISTRUE) {
                                                continue;
                                        }

                                        kportdisplay_print_row(
                                                PROTOCOL,
                                                FAMILY,
                                                PORT,
                                                SLOT
                                        );
                                        (*PRINTED)++;
                                }
                        }
                }
                return;
        }

        for (int PRIORITY = 0; PRIORITY < 3; PRIORITY++) {
                for (unsigned int PROTOCOL = 0; PROTOCOL < KPORTDISPLAY_PROTOCOLS; PROTOCOL++) {
                        for (unsigned int PORT = 1; PORT < MAX_PORTS; PORT++) {
                                for (unsigned int FAMILY = 0; FAMILY < KPORTDISPLAY_FAMILIES; FAMILY++) {
                                        const KPORTDISPLAY_SLOT * SLOT = &SLOTS[
                                                kportdisplay_index(PROTOCOL, FAMILY, PORT)
                                        ];

                                        if (
                                                SLOT->PRESENT != ISTRUE
                                                || kportdisplay_slot_priority(SLOT) != PRIORITY
                                        ) {
                                                continue;
                                        }

                                        if (*PRINTED >= KPORTDISPLAY_TABLE_LIMIT) {
                                                return;
                                        }

                                        kportdisplay_print_row(
                                                PROTOCOL,
                                                FAMILY,
                                                PORT,
                                                SLOT
                                        );
                                        (*PRINTED)++;
                                }
                        }
                }
        }
}

void kportdisplay_target(
        _carry_forward * _prog_data,
        const unsigned char * TID,
        KPORTDISPLAY_MODE MODE,
        unsigned int FILTER_PORT
) {
        char PATH[MAX_PATH];
        char LINE[512];
        KPORTDISPLAY_SLOT * SLOTS;
        FILE * FILE_HANDLE;
        uint64_t TOTAL = 0;
        uint64_t PRINTED = 0;
        uint64_t DETAIL_PRINTED = 0;
        uint64_t OPEN_TOTAL = 0;

        if (
                !_prog_data
                || !TID
                || FILTER_PORT >= MAX_PORTS
        ) {
                return;
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
                return;
        }

        FILE_HANDLE = fopen(PATH, "r");
        if (!FILE_HANDLE) {
                return;
        }

        SLOTS = calloc(
                KPORTDISPLAY_PROTOCOLS
                * KPORTDISPLAY_FAMILIES
                * MAX_PORTS,
                sizeof(*SLOTS)
        );

        if (!SLOTS) {
                fclose(FILE_HANDLE);
                kui_add_line(
                        NOTICE_ERROR
                        "Unable to allocate port display state."
                );
                return;
        }

        while (fgets(LINE, sizeof(LINE), FILE_HANDLE) != NULL) {
                unsigned long long TIMESTAMP_NS;
                unsigned int FAMILY;
                unsigned int PORT;
                unsigned int RX_BYTES;
                char PROTOCOL[8];
                char STATE[32];
                char EVIDENCE[64];
                int PROTOCOL_INDEX;
                int FAMILY_INDEX;
                KPORTSCAN_STATE PARSED_STATE;
                KPORTSCAN_EVIDENCE PARSED_EVIDENCE;
                KPORTDISPLAY_SLOT * SLOT;

                if (LINE[0] == '#') {
                        continue;
                }

                memset(PROTOCOL, 0x00, sizeof(PROTOCOL));
                memset(STATE, 0x00, sizeof(STATE));
                memset(EVIDENCE, 0x00, sizeof(EVIDENCE));

                if (
                        sscanf(
                                LINE,
                                "%llu\t%7[^\t]\t%u\t%u\t%31[^\t]\t%63[^\t]\t%u",
                                &TIMESTAMP_NS,
                                PROTOCOL,
                                &FAMILY,
                                &PORT,
                                STATE,
                                EVIDENCE,
                                &RX_BYTES
                        ) != 7
                ) {
                        continue;
                }

                if (PORT == 0U || PORT >= MAX_PORTS) {
                        continue;
                }

                PROTOCOL_INDEX = kportdisplay_protocol_index(PROTOCOL);
                FAMILY_INDEX = kportdisplay_family_index(FAMILY);
                PARSED_STATE = kportdisplay_parse_state(STATE);
                PARSED_EVIDENCE = kportdisplay_parse_evidence(EVIDENCE);

                if (
                        PROTOCOL_INDEX < 0
                        || FAMILY_INDEX < 0
                        || PARSED_STATE == KPORTSCAN_STATE_UNKNOWN
                ) {
                        continue;
                }

                SLOT = &SLOTS[
                        kportdisplay_index(
                                (unsigned int)PROTOCOL_INDEX,
                                (unsigned int)FAMILY_INDEX,
                                PORT
                        )
                ];

                if (
                        SLOT->PRESENT == ISTRUE
                        && SLOT->TIMESTAMP_NS > (uint64_t)TIMESTAMP_NS
                ) {
                        continue;
                }

                SLOT->TIMESTAMP_NS = (uint64_t)TIMESTAMP_NS;
                SLOT->RX_BYTES = RX_BYTES;
                SLOT->PRESENT = ISTRUE;
                SLOT->STATE = (uint8_t)PARSED_STATE;
                SLOT->EVIDENCE = (uint8_t)PARSED_EVIDENCE;
        }

        fclose(FILE_HANDLE);

        if (FILTER_PORT != 0U) {
                for (unsigned int PROTOCOL = 0; PROTOCOL < KPORTDISPLAY_PROTOCOLS; PROTOCOL++) {
                        for (unsigned int PORT = 1; PORT < MAX_PORTS; PORT++) {
                                for (unsigned int FAMILY = 0; FAMILY < KPORTDISPLAY_FAMILIES; FAMILY++) {
                                        KPORTDISPLAY_SLOT * SLOT = &SLOTS[
                                                kportdisplay_index(PROTOCOL, FAMILY, PORT)
                                        ];

                                        if (
                                                SLOT->PRESENT == ISTRUE
                                                && (
                                                        PROTOCOL != 0U
                                                        || PORT != FILTER_PORT
                                                )
                                        ) {
                                                SLOT->PRESENT = ISFALSE;
                                        }
                                }
                        }
                }
        }

        if (MODE == KPORTDISPLAY_MODE_OBSERVED) {
                for (unsigned int PROTOCOL = 0; PROTOCOL < KPORTDISPLAY_PROTOCOLS; PROTOCOL++) {
                        for (unsigned int PORT = 1; PORT < MAX_PORTS; PORT++) {
                                for (unsigned int FAMILY = 0; FAMILY < KPORTDISPLAY_FAMILIES; FAMILY++) {
                                        KPORTDISPLAY_SLOT * SLOT = &SLOTS[
                                                kportdisplay_index(PROTOCOL, FAMILY, PORT)
                                        ];

                                        if (
                                                SLOT->PRESENT == ISTRUE
                                                && SLOT->RX_BYTES == 0U
                                        ) {
                                                SLOT->PRESENT = ISFALSE;
                                        }
                                }
                        }
                }
        }

        for (unsigned int PROTOCOL = 0; PROTOCOL < KPORTDISPLAY_PROTOCOLS; PROTOCOL++) {
                for (unsigned int PORT = 1; PORT < MAX_PORTS; PORT++) {
                        for (unsigned int FAMILY = 0; FAMILY < KPORTDISPLAY_FAMILIES; FAMILY++) {
                                KPORTDISPLAY_SLOT * SLOT = &SLOTS[
                                        kportdisplay_index(PROTOCOL, FAMILY, PORT)
                                ];

                                if (SLOT->PRESENT != ISTRUE) {
                                        continue;
                                }

                                TOTAL++;
                                if (SLOT->STATE == KPORTSCAN_STATE_OPEN) {
                                        OPEN_TOTAL++;
                                }
                        }
                }
        }

        if (TOTAL == 0) {
                free(SLOTS);
                return;
        }

        kui_add_line("");
        kui_add_line(
                ANSI_COLOR_CYAN
                " PORTS:"
                ANSI_COLOR_RESET
        );
        kui_add_line(
                RGB_COLOR_SOFT_GREY
                " ─────────────────────────────────────────────────────────"
                ANSI_COLOR_RESET
        );
        kui_add_line(" PROTO   PORT   AF     STATE           RETURN");
        kui_add_line(
                RGB_COLOR_SOFT_GREY
                " ─────────────────────────────────────────────────────────"
                ANSI_COLOR_RESET
        );

        kportdisplay_print_sorted_table(
                SLOTS,
                TOTAL,
                &PRINTED
        );

        if (PRINTED < TOTAL) {
                kui_add_line(
                        NOTICE_INFO
                        "%llu additional port observations omitted from the default display.",
                        (unsigned long long)(TOTAL - PRINTED)
                );
        }

        if (
                MODE == KPORTDISPLAY_MODE_DETAIL_OPEN
                || MODE == KPORTDISPLAY_MODE_DETAIL_ALL
        ) {
                for (unsigned int PROTOCOL = 0; PROTOCOL < KPORTDISPLAY_PROTOCOLS; PROTOCOL++) {
                        for (unsigned int PORT = 1; PORT < MAX_PORTS; PORT++) {
                                for (unsigned int FAMILY = 0; FAMILY < KPORTDISPLAY_FAMILIES; FAMILY++) {
                                        KPORTDISPLAY_SLOT * SLOT = &SLOTS[
                                                kportdisplay_index(PROTOCOL, FAMILY, PORT)
                                        ];

                                        if (SLOT->PRESENT != ISTRUE) {
                                                continue;
                                        }

                                        if (
                                                MODE == KPORTDISPLAY_MODE_DETAIL_OPEN
                                                && SLOT->STATE != KPORTSCAN_STATE_OPEN
                                        ) {
                                                continue;
                                        }

                                        if (DETAIL_PRINTED >= KPORTDISPLAY_DETAIL_LIMIT) {
                                                continue;
                                        }

                                        kportdisplay_print_detail(
                                                PROTOCOL,
                                                FAMILY,
                                                PORT,
                                                SLOT
                                        );
                                        if (SLOT->STATE == KPORTSCAN_STATE_OPEN) {
                                                kbanner_display_port(
                                                        _prog_data,
                                                        TID,
                                                        PROTOCOL,
                                                        FAMILY,
                                                        PORT
                                                );
                                        }
                                        DETAIL_PRINTED++;
                                }
                        }
                }

                if (
                        MODE == KPORTDISPLAY_MODE_DETAIL_OPEN
                        && DETAIL_PRINTED < OPEN_TOTAL
                ) {
                        kui_add_line(
                                NOTICE_INFO
                                "%llu additional open-port detail blocks omitted.",
                                (unsigned long long)(OPEN_TOTAL - DETAIL_PRINTED)
                        );
                } else if (
                        MODE == KPORTDISPLAY_MODE_DETAIL_ALL
                        && DETAIL_PRINTED < TOTAL
                ) {
                        kui_add_line(
                                NOTICE_INFO
                                "%llu additional port detail blocks omitted.",
                                (unsigned long long)(TOTAL - DETAIL_PRINTED)
                        );
                }
        }

        free(SLOTS);
}


int8_t kportdisplay_has_open_tcp_ports(
        _carry_forward * _prog_data,
        const unsigned char * TID,
        const KPORT_SPEC * PORTS
) {
        char PATH[MAX_PATH];

        if (!_prog_data || !TID || !PORTS || PORTS->count == 0U) {
                return ISFALSE;
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
                return ISFALSE;
        }

        return kportselect_file_has_open_tcp_ports(PATH, PORTS) == 1
                ? ISTRUE
                : ISFALSE;
}

int8_t kportdisplay_has_open_tcp_port(
        _carry_forward * _prog_data,
        const unsigned char * TID,
        unsigned int PORT
) {
        char EXPRESSION[16];
        KPORT_SPEC PORTS = {0};

        if (PORT == 0U || PORT >= MAX_PORTS) {
                return ISFALSE;
        }

        if (
                snprintf(EXPRESSION, sizeof(EXPRESSION), "%u", PORT)
                        >= (int)sizeof(EXPRESSION)
                || kportspec_parse(EXPRESSION, &PORTS) != NORMAL
        ) {
                return ISFALSE;
        }

        return kportdisplay_has_open_tcp_ports(_prog_data, TID, &PORTS);
}


int8_t kportdisplay_has_observed(
        _carry_forward * _prog_data,
        const unsigned char * TID
) {
        char PATH[MAX_PATH];
        char LINE[512];
        FILE * FILE_HANDLE;

        if (!_prog_data || !TID) {
                return ISFALSE;
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
                return ISFALSE;
        }

        FILE_HANDLE = fopen(PATH, "r");
        if (!FILE_HANDLE) {
                return ISFALSE;
        }

        while (fgets(LINE, sizeof(LINE), FILE_HANDLE) != NULL) {
                unsigned int RX_BYTES = 0;
                unsigned long long TIMESTAMP_NS;
                unsigned int FAMILY;
                unsigned int PORT;
                char PROTOCOL[8];
                char STATE[32];
                char EVIDENCE[64];

                if (LINE[0] == '#') {
                        continue;
                }

                if (
                        sscanf(
                                LINE,
                                "%llu\t%7[^\t]\t%u\t%u\t%31[^\t]\t%63[^\t]\t%u",
                                &TIMESTAMP_NS, PROTOCOL, &FAMILY, &PORT,
                                STATE, EVIDENCE, &RX_BYTES
                        ) == 7
                        && RX_BYTES > 0U
                ) {
                        fclose(FILE_HANDLE);
                        return ISTRUE;
                }
        }

        fclose(FILE_HANDLE);
        return ISFALSE;
}
