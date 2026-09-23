// Copyright 2026 Jamison A. Drapeau
#ifndef TARGETS_NEIGHBOR_FILTER_H
#define TARGETS_NEIGHBOR_FILTER_H

#include "data.h"
#include <stdint.h>

// -n is deliberately fail-closed: a timeout, a generic error, or a target
// that has never been scanned does not prove a neighbor-resolution failure.
// SCAN_RESULT_NEIGHBOR denotes NOSIX_ERR_NEIGHBOR (a TX error), not an ARP/NDP reply.
static inline int8_t targets_prune_no_neighbor(
        const TARGET * PETAL,
        uint8_t ICMPV6_STATE,
        int8_t OBSERVED
) {
        const uint8_t NEIGHBOR_FAILURE = (
                SCAN_RESULT_RAN | SCAN_RESULT_ERROR | SCAN_RESULT_NEIGHBOR
        );
        int8_t HAS_ADDRESS = ISFALSE;

        if (
                !PETAL
                || PETAL->SCAN.SCAN_VERSION != TARGET_SCAN_DATA_VERSION
                || OBSERVED != ISFALSE
        ) {
                return ISFALSE;
        }

        if (PETAL->IPV4[0] != 0x00) {
                HAS_ADDRESS = ISTRUE;
                if (PETAL->SCAN.ICMPV4 != NEIGHBOR_FAILURE) {
                        return ISFALSE;
                }
        }

        if (PETAL->IPV6[0] != 0x00) {
                HAS_ADDRESS = ISTRUE;
                if (ICMPV6_STATE != NEIGHBOR_FAILURE) {
                        return ISFALSE;
                }
        }

        return HAS_ADDRESS;
}

#endif
