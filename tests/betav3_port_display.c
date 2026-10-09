// Copyright 2026 Jamison A. Drapeau
// Display policy: hide negative port outcomes without hiding TCP SYN+ACK proof.
#include "kportdisplay_policy.h"

#include <stdio.h>

typedef struct PORT_CASE {
        const char * NAME;
        unsigned int PROTOCOL;
        uint8_t STATE;
        uint8_t EVIDENCE;
        uint8_t PAST_TCP_SYNACK;
        int VISIBLE;
        int HISTORICAL;
} PORT_CASE;

int main(void) {
        static const PORT_CASE CASES[] = {
                {"TCP open SYN+ACK", 0, KPORTSCAN_STATE_OPEN,
                        KPORTSCAN_EVIDENCE_TCP_SYN_ACK, 0, 1, 0},
                {"TCP open no banner", 0, KPORTSCAN_STATE_OPEN,
                        KPORTSCAN_EVIDENCE_NONE, 0, 1, 0},
                {"TCP closed RST", 0, KPORTSCAN_STATE_CLOSED,
                        KPORTSCAN_EVIDENCE_TCP_RST, 0, 0, 0},
                {"TCP filtered timeout", 0, KPORTSCAN_STATE_FILTERED,
                        KPORTSCAN_EVIDENCE_TIMEOUT, 0, 0, 0},
                {"TCP closed with prior SYN+ACK", 0, KPORTSCAN_STATE_CLOSED,
                        KPORTSCAN_EVIDENCE_TCP_RST, 1, 1, 1},
                {"TCP filtered with prior SYN+ACK", 0, KPORTSCAN_STATE_FILTERED,
                        KPORTSCAN_EVIDENCE_TIMEOUT, 1, 1, 1},
                {"TCP error with prior SYN+ACK", 0, KPORTSCAN_STATE_ERROR,
                        KPORTSCAN_EVIDENCE_NETWORK_ERROR, 1, 1, 1},
                {"TCP closed conflicting SYN+ACK without history", 0,
                        KPORTSCAN_STATE_CLOSED,
                        KPORTSCAN_EVIDENCE_TCP_SYN_ACK, 0, 0, 0},
                {"UDP open reply", 1, KPORTSCAN_STATE_OPEN,
                        KPORTSCAN_EVIDENCE_UDP_REPLY, 0, 1, 0},
                {"UDP closed ICMP unreachable", 1, KPORTSCAN_STATE_CLOSED,
                        KPORTSCAN_EVIDENCE_ICMP_PORT_UNREACHABLE, 0, 0, 0},
                {"UDP filtered ICMP", 1, KPORTSCAN_STATE_FILTERED,
                        KPORTSCAN_EVIDENCE_ICMP_FILTERED, 0, 0, 0},
                {"UDP open filtered timeout", 1, KPORTSCAN_STATE_OPEN_FILTERED,
                        KPORTSCAN_EVIDENCE_TIMEOUT, 0, 0, 0},
                {"UDP open filtered no TCP override", 1, KPORTSCAN_STATE_OPEN_FILTERED,
                        KPORTSCAN_EVIDENCE_TIMEOUT, 1, 0, 0},
                {"TCP error still diagnostic", 0, KPORTSCAN_STATE_ERROR,
                        KPORTSCAN_EVIDENCE_NETWORK_ERROR, 0, 1, 0},
                {"UDP error still diagnostic", 1, KPORTSCAN_STATE_ERROR,
                        KPORTSCAN_EVIDENCE_NETWORK_ERROR, 0, 1, 0}
        };
        int failed = 0;

        for (size_t i = 0; i < sizeof(CASES) / sizeof(CASES[0]); ++i) {
                const PORT_CASE * C = &CASES[i];
                int visible = kportdisplay_visible(
                        C->PROTOCOL, C->STATE, C->PAST_TCP_SYNACK
                );
                int historical = kportdisplay_use_historical_tcp(
                        C->PROTOCOL, C->STATE, C->PAST_TCP_SYNACK
                );

                if (visible != C->VISIBLE || historical != C->HISTORICAL) {
                        fprintf(stderr,
                                "FAIL: %s (visible=%d expected=%d; historical=%d expected=%d)\n",
                                C->NAME, visible, C->VISIBLE,
                                historical, C->HISTORICAL);
                        failed++;
                }
        }

        if (!kportdisplay_is_tcp_synack(
                        0, KPORTSCAN_STATE_OPEN, KPORTSCAN_EVIDENCE_TCP_SYN_ACK)
                || kportdisplay_is_tcp_synack(
                        0, KPORTSCAN_STATE_CLOSED, KPORTSCAN_EVIDENCE_TCP_RST)
                || kportdisplay_is_tcp_synack(
                        1, KPORTSCAN_STATE_OPEN, KPORTSCAN_EVIDENCE_UDP_REPLY)) {
                fputs("FAIL: TCP positive evidence classification\n", stderr);
                failed++;
        }
        if (failed != 0) return 1;
        printf("BetaV3 port display: %zu filter/connection cases PASS\n",
                sizeof(CASES) / sizeof(CASES[0]));
        return 0;
}
