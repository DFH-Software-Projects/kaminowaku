// Copyright 2026 Jamison A. Drapeau
// BetaV3 Phase 5: targets del -n must remain fail-closed for each IP family.
#include "targets_neighbor_filter.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(sizeof(TARGET_SCAN_DATA) == LAST_BLOCK,
        "Phase 5 must not change the TARGET scan storage layout");
_Static_assert(SCAN_RESULT_L3_FALLBACK == (1U << 6),
        "Unexpected NOSIX L3 result bit");

#define FAILURE_NEIGHBOR (SCAN_RESULT_RAN | SCAN_RESULT_ERROR | SCAN_RESULT_NEIGHBOR)
#define FAILURE_L3       (SCAN_RESULT_RAN | SCAN_RESULT_ERROR | SCAN_RESULT_L3_FALLBACK)

static int TESTS = 0;
static int ERRORS = 0;

static void check(
        const char *NAME,
        const TARGET *TARGET_DATA,
        uint8_t IPV6_STATE,
        int8_t OBSERVED,
        int8_t EXPECTED
) {
        int8_t actual = targets_prune_transport_failure(
                TARGET_DATA, IPV6_STATE, OBSERVED
        );

        TESTS++;
        if (actual != EXPECTED) {
                fprintf(stderr, "FAIL %s: expected %d, got %d\n",
                        NAME, (int)EXPECTED, (int)actual);
                ERRORS++;
        }
}

int main(void) {
        TARGET *t = calloc(1, sizeof(*t));
        if (!t) return 2;

        t->SCAN.SCAN_VERSION = TARGET_SCAN_DATA_VERSION;
        t->IPV4[0] = 192;

        check("null target", NULL, 0, ISFALSE, ISFALSE);
        check("no scan", t, 0, ISFALSE, ISFALSE);
        check("generic scan error", t, 0, ISFALSE, ISFALSE);
        t->SCAN.ICMPV4 = SCAN_RESULT_RAN | SCAN_RESULT_ERROR;
        check("generic TX error", t, 0, ISFALSE, ISFALSE);
        t->SCAN.ICMPV4 = SCAN_RESULT_RAN | SCAN_RESULT_TIMEOUT;
        check("timeout after L3 TX success", t, 0, ISFALSE, ISFALSE);
        t->SCAN.ICMPV4 = SCAN_RESULT_RAN | SCAN_RESULT_ERROR | SCAN_RESULT_ROUTE;
        check("route error", t, 0, ISFALSE, ISFALSE);
        t->SCAN.ICMPV4 = SCAN_RESULT_RAN | SCAN_RESULT_REPLY;
        check("echo reply", t, 0, ISFALSE, ISFALSE);
        t->SCAN.ICMPV4 = SCAN_RESULT_ERROR | SCAN_RESULT_L3_FALLBACK;
        check("L3 failure without RAN", t, 0, ISFALSE, ISFALSE);
        t->SCAN.ICMPV4 = SCAN_RESULT_RAN | SCAN_RESULT_L3_FALLBACK;
        check("L3 failure without ERROR", t, 0, ISFALSE, ISFALSE);

        t->SCAN.ICMPV4 = FAILURE_NEIGHBOR;
        check("IPv4 neighbor failure", t, 0, ISFALSE, ISTRUE);
        t->SCAN.ICMPV4 = FAILURE_L3;
        check("IPv4 L3 failure", t, 0, ISFALSE, ISTRUE);
        check("observed ICMP or port response", t, 0, ISTRUE, ISFALSE);
        check("observation not conclusively false", t, 0, -1, ISFALSE);
        t->SCAN.ICMPV4 = FAILURE_L3 | SCAN_RESULT_REPLY;
        check("conflicting response bit", t, 0, ISFALSE, ISFALSE);
        t->SCAN.ICMPV4 = FAILURE_L3 | SCAN_RESULT_NEIGHBOR;
        check("ambiguous L2+L3 failure", t, 0, ISFALSE, ISFALSE);
        t->SCAN.ICMPV4 = FAILURE_L3 | (1U << 7);
        check("unknown result bit", t, 0, ISFALSE, ISFALSE);
        t->SCAN.ICMPV4 = FAILURE_L3;
        t->SCAN.SCAN_VERSION = TARGET_SCAN_DATA_VERSION + 1;
        check("invalid scan version", t, 0, ISFALSE, ISFALSE);
        t->SCAN.SCAN_VERSION = TARGET_SCAN_DATA_VERSION;

        t->IPV4[0] = 0;
        check("no IP address", t, 0, ISFALSE, ISFALSE);
        t->IPV6[0] = 0x20;
        check("IPv6 untested", t, 0, ISFALSE, ISFALSE);
        check("IPv6 L2 neighbor failure", t, FAILURE_NEIGHBOR, ISFALSE, ISTRUE);
        check("IPv6 L3 failure", t, FAILURE_L3, ISFALSE, ISTRUE);
        check("IPv6 generic TX error", t, SCAN_RESULT_RAN | SCAN_RESULT_ERROR,
              ISFALSE, ISFALSE);
        check("IPv6 timeout", t, SCAN_RESULT_RAN | SCAN_RESULT_TIMEOUT,
              ISFALSE, ISFALSE);
        check("IPv6 reply", t, SCAN_RESULT_RAN | SCAN_RESULT_REPLY,
              ISFALSE, ISFALSE);
        check("IPv6 positive response elsewhere", t, FAILURE_L3, ISTRUE, ISFALSE);

        t->IPV4[0] = 192;
        t->SCAN.ICMPV4 = FAILURE_L3;
        check("dual-stack L3+L2", t, FAILURE_NEIGHBOR, ISFALSE, ISTRUE);
        check("dual-stack L3+L3", t, FAILURE_L3, ISFALSE, ISTRUE);
        check("dual-stack L3+untested", t, 0, ISFALSE, ISFALSE);
        check("dual-stack L3+timeout", t, SCAN_RESULT_RAN | SCAN_RESULT_TIMEOUT,
              ISFALSE, ISFALSE);
        check("dual-stack L3+reply", t, SCAN_RESULT_RAN | SCAN_RESULT_REPLY,
              ISFALSE, ISFALSE);

        t->SCAN.ICMPV4 = FAILURE_NEIGHBOR;
        check("dual-stack L2+L3", t, FAILURE_L3, ISFALSE, ISTRUE);
        check("dual-stack L2+L2", t, FAILURE_NEIGHBOR, ISFALSE, ISTRUE);
        check("dual-stack observed TCP response", t, FAILURE_NEIGHBOR,
              ISTRUE, ISFALSE);

        // An unambiguous TCP/UDP-only network-error result is not stored
        // in the ICMP scan slot; -n must not guess from per-port state.
        t->SCAN.ICMPV4 = 0;
        t->PORTS[80] = PORT_CLOSED;
        check("TCP-only result does not qualify", t, FAILURE_L3,
              ISFALSE, ISFALSE);

        free(t);
        if (ERRORS != 0) {
                fprintf(stderr, "BetaV3 Phase 5: %d/%d target-pruning checks FAILED\n",
                        ERRORS, TESTS);
                return 1;
        }

        printf("BetaV3 Phase 5: %d target-pruning checks PASSED\n", TESTS);
        return 0;
}
