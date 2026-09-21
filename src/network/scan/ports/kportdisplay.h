// Copyright 2026 Jamison A. Drapeau
#ifndef __KPORTDISPLAY__H
#define __KPORTDISPLAY__H

#include "data.h"

typedef enum KPORTDISPLAY_MODE {
        KPORTDISPLAY_MODE_COMPACT = 0,
        KPORTDISPLAY_MODE_DETAIL_OPEN,
        KPORTDISPLAY_MODE_DETAIL_ALL,
        KPORTDISPLAY_MODE_OBSERVED
} KPORTDISPLAY_MODE;

void kportdisplay_target(
        _carry_forward * _prog_data,
        const unsigned char * TID,
        KPORTDISPLAY_MODE MODE,
        unsigned int FILTER_PORT
);

int8_t kportdisplay_has_open_tcp_port(
        _carry_forward * _prog_data,
        const unsigned char * TID,
        unsigned int PORT
);

int8_t kportdisplay_has_observed(
        _carry_forward * _prog_data,
        const unsigned char * TID
);

#endif
