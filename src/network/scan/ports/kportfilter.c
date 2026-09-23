// Copyright 2026 Jamison A. Drapeau
#include "kportfilter.h"

int8_t kportfilter_slot_included(
        const KPORT_SPEC * FILTER,
        unsigned int PROTOCOL_INDEX,
        unsigned int PORT
) {
        if (PROTOCOL_INDEX > 1U || PORT == 0U || PORT >= KPORTSPEC_MAX_PORTS) {
                return 0;
        }

        if (!FILTER) {
                return 1;
        }

        if (FILTER->count == 0U || PROTOCOL_INDEX != 0U) {
                return 0;
        }

        return kportspec_contains(FILTER, PORT);
}
