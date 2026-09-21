// Copyright 2026 Jamison A. Drapeau
#ifndef __KBANNER__H
#define __KBANNER__H

#include "data.h"

#include <stdint.h>

int kbanner_enrich_tcp(
        _carry_forward * _prog_data,
        const unsigned char * TID,
        const char * ADDRESS,
        nosix_address_family_t FAMILY,
        uint64_t SCAN_START_NS
);

void kbanner_display_port(
        _carry_forward * _prog_data,
        const unsigned char * TID,
        unsigned int PROTOCOL_INDEX,
        unsigned int FAMILY_INDEX,
        unsigned int PORT
);

#endif
