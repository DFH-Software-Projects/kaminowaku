// Copyright 2026 Jamison A. Drapeau
#include "ktargetdisplay_args.h"

#include <string.h>

int ktargetdisplay_parse_args(
        unsigned char * const * TOKENS,
        size_t TOKEN_COUNT,
        KTARGETDISPLAY_ARGS * OUT
) {
        KTARGETDISPLAY_ARGS DRAFT = {0};
        const char * FLAG;

        if (!TOKENS || !OUT || TOKEN_COUNT < 2 || !TOKENS[0] || !TOKENS[1]) {
                return -1;
        }

        if (TOKEN_COUNT == 2) {
                *OUT = DRAFT;
                return 0;
        }

        if (!TOKENS[2]) {
                return -1;
        }

        FLAG = (const char *)TOKENS[2];

        if (TOKEN_COUNT == 3) {
                if (strcmp(FLAG, "-d") == 0) {
                        DRAFT.MODE = KTARGETDISPLAY_ARG_DETAIL_ALL;
                } else if (strcmp(FLAG, "-o") == 0) {
                        DRAFT.MODE = KTARGETDISPLAY_ARG_OBSERVED;
                } else {
                        return -1;
                }
                *OUT = DRAFT;
                return 0;
        }

        if (strcmp(FLAG, "-p") == 0) {
                DRAFT.MODE = KTARGETDISPLAY_ARG_PORT_COMPACT;
        } else if (strcmp(FLAG, "-b") == 0) {
                DRAFT.MODE = KTARGETDISPLAY_ARG_PORT_DETAIL_OPEN;
        } else {
                return -1;
        }

        for (size_t INDEX = 3; INDEX < TOKEN_COUNT; INDEX++) {
                if (
                        !TOKENS[INDEX]
                        || kportspec_extend(
                                (const char *)TOKENS[INDEX],
                                &DRAFT.PORTS
                        ) != 0
                ) {
                        return -1;
                }
        }

        if (DRAFT.PORTS.count == 0) {
                return -1;
        }

        *OUT = DRAFT;
        return 0;
}
