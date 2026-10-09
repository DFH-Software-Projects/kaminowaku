// Copyright 2026 Jamison A. Drapeau
#ifndef TARGETS_NEIGHBOR_FILTER_H
#define TARGETS_NEIGHBOR_FILTER_H

#include "data.h"
#include <stdint.h>

// -n is deliberately fail-closed: a timeout, a generic error, or a target
// that has never been scanned is not conclusive transport-failure evidence.
// Accept only the exact NOSIX neighbor-resolution or attempted L3 TX failure.
// SCAN_RESULT_NEIGHBOR means NOSIX_ERR_NEIGHBOR, not an ARP/NDP response.
// SCAN_RESULT_L3_FALLBACK means NOSIX_ERR_L3_FALLBACK_FAILED, not a
// missing reply after a successful L3 transmission.
static inline int8_t targets_prune_transport_failure_state(uint8_t RESULT) {
        const uint8_t REQUIRED = SCAN_RESULT_RAN | SCAN_RESULT_ERROR;

        return (
                RESULT == (uint8_t)(REQUIRED | SCAN_RESULT_NEIGHBOR)
                || RESULT == (uint8_t)(REQUIRED | SCAN_RESULT_L3_FALLBACK)
        ) ? ISTRUE : ISFALSE;
}

static inline int8_t targets_prune_transport_failure(
        const TARGET * PETAL,
        uint8_t ICMPV6_STATE,
        int8_t OBSERVED
) {
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
                if (targets_prune_transport_failure_state(PETAL->SCAN.ICMPV4) != ISTRUE) {
                        return ISFALSE;
                }
        }

        if (PETAL->IPV6[0] != 0x00) {
                HAS_ADDRESS = ISTRUE;
                if (targets_prune_transport_failure_state(ICMPV6_STATE) != ISTRUE) {
                        return ISFALSE;
                }
        }

        return HAS_ADDRESS;
}

#endif
