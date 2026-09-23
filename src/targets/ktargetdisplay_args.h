// Copyright 2026 Jamison A. Drapeau
#ifndef __KTARGETDISPLAY_ARGS__H
#define __KTARGETDISPLAY_ARGS__H

#include "kportspec.h"
#include <stddef.h>

typedef enum KTARGETDISPLAY_ARG_MODE {
        KTARGETDISPLAY_ARG_COMPACT = 0,
        KTARGETDISPLAY_ARG_DETAIL_ALL,
        KTARGETDISPLAY_ARG_OBSERVED,
        KTARGETDISPLAY_ARG_PORT_COMPACT,
        KTARGETDISPLAY_ARG_PORT_DETAIL_OPEN
} KTARGETDISPLAY_ARG_MODE;

typedef struct KTARGETDISPLAY_ARGS {
        KTARGETDISPLAY_ARG_MODE MODE;
        KPORT_SPEC PORTS;
} KTARGETDISPLAY_ARGS;

// Parse the full "targets display" token sequence, including optional port
// expressions. Each expression is unioned into PORTS using the scanner grammar.
// On failure, OUT is unchanged; a successful unfiltered request has count == 0.
int ktargetdisplay_parse_args(
        unsigned char * const * TOKENS,
        size_t TOKEN_COUNT,
        KTARGETDISPLAY_ARGS * OUT
);

#endif
