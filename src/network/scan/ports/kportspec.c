// Copyright 2026 Jamison A. Drapeau
#define _POSIX_C_SOURCE 200809L

#include "kportspec.h"

#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

// These parsing rules intentionally match kportscan.c in v0.1.1.
// In particular, strtok_r() skips empty comma fields; changing that grammar
// is a separate decision from extracting the shared parser.

static int kportspec_set_port(KPORT_SPEC * SPEC, unsigned long PORT) {
        size_t BYTE;
        uint8_t MASK;

        if (!SPEC || PORT == 0UL || PORT >= KPORTSPEC_MAX_PORTS) {
                return -1;
        }

        BYTE = (size_t)PORT >> 3;
        MASK = (uint8_t)(1U << (PORT & 7U));

        if ((SPEC->bits[BYTE] & MASK) == 0U) {
                SPEC->bits[BYTE] |= MASK;
                SPEC->count++;
        }

        return 0;
}

static int kportspec_parse_number(const char * TEXT, unsigned long * VALUE) {
        char * END = NULL;
        unsigned long PARSED;

        if (!TEXT || TEXT[0] == '\0' || !VALUE) {
                return -1;
        }

        errno = 0;
        PARSED = strtoul(TEXT, &END, 10);
        if (
                errno != 0
                || !END
                || *END != '\0'
                || PARSED == 0UL
                || PARSED >= KPORTSPEC_MAX_PORTS
        ) {
                return -1;
        }

        *VALUE = PARSED;
        return 0;
}

static int kportspec_parse_segment(char * SEGMENT, KPORT_SPEC * SPEC) {
        char * DASH;
        unsigned long FIRST;
        unsigned long LAST;

        if (!SEGMENT || SEGMENT[0] == '\0' || !SPEC) {
                return -1;
        }

        DASH = strchr(SEGMENT, '-');
        if (!DASH) {
                if (kportspec_parse_number(SEGMENT, &FIRST) != 0) {
                        return -1;
                }
                return kportspec_set_port(SPEC, FIRST);
        }

        if (strchr(DASH + 1, '-') != NULL) {
                return -1;
        }

        *DASH = '\0';
        if (
                kportspec_parse_number(SEGMENT, &FIRST) != 0
                || kportspec_parse_number(DASH + 1, &LAST) != 0
                || LAST < FIRST
        ) {
                return -1;
        }

        for (unsigned long PORT = FIRST; PORT <= LAST; PORT++) {
                if (kportspec_set_port(SPEC, PORT) != 0) {
                        return -1;
                }
        }

        return 0;
}

static int kportspec_parse_expression(const char * EXPRESSION, KPORT_SPEC * SPEC) {
        char * COPY;
        char * SAVE = NULL;
        char * SEGMENT;
        int STATUS = 0;

        if (!EXPRESSION || !SPEC) {
                return -1;
        }

        COPY = strdup(EXPRESSION);
        if (!COPY) {
                return -1;
        }

        SEGMENT = strtok_r(COPY, ",", &SAVE);
        if (!SEGMENT) {
                STATUS = -1;
                goto CLEANUP;
        }

        while (SEGMENT) {
                if (kportspec_parse_segment(SEGMENT, SPEC) != 0) {
                        STATUS = -1;
                        goto CLEANUP;
                }
                SEGMENT = strtok_r(NULL, ",", &SAVE);
        }

CLEANUP:
        free(COPY);
        return STATUS;
}

int kportspec_parse(const char * EXPRESSION, KPORT_SPEC * SPEC) {
        KPORT_SPEC DRAFT = {0};

        if (!SPEC || kportspec_parse_expression(EXPRESSION, &DRAFT) != 0) {
                return -1;
        }

        *SPEC = DRAFT;
        return 0;
}

int kportspec_extend(const char * EXPRESSION, KPORT_SPEC * SPEC) {
        KPORT_SPEC DRAFT;

        if (!SPEC) {
                return -1;
        }

        DRAFT = *SPEC;
        if (kportspec_parse_expression(EXPRESSION, &DRAFT) != 0) {
                return -1;
        }

        *SPEC = DRAFT;
        return 0;
}

int8_t kportspec_contains(const KPORT_SPEC * SPEC, unsigned int PORT) {
        if (!SPEC || PORT == 0U || PORT >= KPORTSPEC_MAX_PORTS) {
                return 0;
        }

        return (SPEC->bits[PORT >> 3] & (uint8_t)(1U << (PORT & 7U))) ? 1 : 0;
}
