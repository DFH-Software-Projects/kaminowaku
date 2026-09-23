// Copyright 2026 Jamison A. Drapeau
#include "kportselect.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KPORTSELECT_FAMILIES 2U
#define KPORTSELECT_STATE_UNKNOWN 0U
#define KPORTSELECT_STATE_OPEN    1U
#define KPORTSELECT_STATE_OTHER   2U

static int kportselect_family_index(unsigned int FAMILY) {
        if (FAMILY == 4U) return 0;
        if (FAMILY == 6U) return 1;
        return -1;
}

static uint8_t kportselect_state(const char * STATE) {
        if (!STATE) return KPORTSELECT_STATE_UNKNOWN;
        if (strcmp(STATE, "OPEN") == 0) return KPORTSELECT_STATE_OPEN;
        if (
                strcmp(STATE, "CLOSED") == 0
                || strcmp(STATE, "FILTERED") == 0
                || strcmp(STATE, "OPEN|FILTERED") == 0
                || strcmp(STATE, "ERROR") == 0
        ) {
                return KPORTSELECT_STATE_OTHER;
        }
        return KPORTSELECT_STATE_UNKNOWN;
}

int8_t kportselect_file_has_open_tcp_ports(
        const char * PATH,
        const KPORT_SPEC * PORTS
) {
        FILE * FILE_HANDLE;
        char LINE[512];
        uint64_t * LATEST;
        uint8_t * STATE;
        size_t SLOT_COUNT = KPORTSELECT_FAMILIES * KPORTSPEC_MAX_PORTS;
        int8_t MATCHED = 0;

        if (!PATH || !PORTS || PORTS->count == 0U) {
                return 0;
        }

        FILE_HANDLE = fopen(PATH, "r");
        if (!FILE_HANDLE) {
                return 0;
        }

        LATEST = calloc(SLOT_COUNT, sizeof(*LATEST));
        STATE = calloc(SLOT_COUNT, sizeof(*STATE));
        if (!LATEST || !STATE) {
                free(LATEST);
                free(STATE);
                fclose(FILE_HANDLE);
                return 0;
        }

        while (fgets(LINE, sizeof(LINE), FILE_HANDLE) != NULL) {
                unsigned long long TIMESTAMP_NS;
                unsigned int FAMILY;
                unsigned int PORT;
                unsigned int RX_BYTES;
                char PROTOCOL[8];
                char STATE_TEXT[32];
                char EVIDENCE[64];
                int FAMILY_INDEX;
                uint8_t PARSED_STATE;
                size_t INDEX;

                if (LINE[0] == '#') {
                        continue;
                }

                if (
                        sscanf(
                                LINE,
                                "%llu\t%7[^\t]\t%u\t%u\t%31[^\t]\t%63[^\t]\t%u",
                                &TIMESTAMP_NS,
                                PROTOCOL,
                                &FAMILY,
                                &PORT,
                                STATE_TEXT,
                                EVIDENCE,
                                &RX_BYTES
                        ) != 7
                        || strcmp(PROTOCOL, "TCP") != 0
                        || kportspec_contains(PORTS, PORT) != 1
                ) {
                        continue;
                }

                FAMILY_INDEX = kportselect_family_index(FAMILY);
                PARSED_STATE = kportselect_state(STATE_TEXT);
                if (FAMILY_INDEX < 0 || PARSED_STATE == KPORTSELECT_STATE_UNKNOWN) {
                        continue;
                }

                INDEX = (size_t)FAMILY_INDEX * KPORTSPEC_MAX_PORTS + PORT;
                if (LATEST[INDEX] > (uint64_t)TIMESTAMP_NS) {
                        continue;
                }

                LATEST[INDEX] = (uint64_t)TIMESTAMP_NS;
                STATE[INDEX] = PARSED_STATE;
        }

        fclose(FILE_HANDLE);

        for (unsigned int PORT = 1U; PORT < KPORTSPEC_MAX_PORTS; PORT++) {
                if (kportspec_contains(PORTS, PORT) != 1) {
                        continue;
                }

                for (unsigned int FAMILY = 0U; FAMILY < KPORTSELECT_FAMILIES; FAMILY++) {
                        size_t INDEX = (size_t)FAMILY * KPORTSPEC_MAX_PORTS + PORT;
                        if (STATE[INDEX] == KPORTSELECT_STATE_OPEN) {
                                MATCHED = 1;
                                goto CLEANUP;
                        }
                }
        }

CLEANUP:
        free(LATEST);
        free(STATE);
        return MATCHED;
}
