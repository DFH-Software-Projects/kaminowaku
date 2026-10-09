// Copyright 2026 Jamison A. Drapeau
#ifndef KPORTDISPLAY_POLICY_H
#define KPORTDISPLAY_POLICY_H

#include "kportscan.h"

/*
 * Port scan results remain intact on disk. Display omits closed, filtered,
 * and UDP OPEN|FILTERED (a timeout without positive service evidence).
 * TCP SYN+ACK is positive reachability evidence, not a completed handshake.
 */
static inline int kportdisplay_is_negative(uint8_t state) {
        return state == KPORTSCAN_STATE_CLOSED
                || state == KPORTSCAN_STATE_FILTERED
                || state == KPORTSCAN_STATE_OPEN_FILTERED;
}

static inline int kportdisplay_is_tcp_synack(
        unsigned int protocol_index,
        uint8_t state,
        uint8_t evidence
) {
        return protocol_index == 0U
                && state == KPORTSCAN_STATE_OPEN
                && evidence == KPORTSCAN_EVIDENCE_TCP_SYN_ACK;
}

/* Keep positive TCP evidence even when the latest scan was negative or
 * inconclusive. The renderer labels this as OPEN (prior), not current.
 */
static inline int kportdisplay_use_historical_tcp(
        unsigned int protocol_index,
        uint8_t state,
        uint8_t prior_synack
) {
        return protocol_index == 0U
                && prior_synack != 0U
                && state != KPORTSCAN_STATE_OPEN;
}

static inline int kportdisplay_visible(
        unsigned int protocol_index,
        uint8_t state,
        uint8_t prior_synack
) {
        return !kportdisplay_is_negative(state)
                || kportdisplay_use_historical_tcp(
                        protocol_index, state, prior_synack
                );
}

#endif
