// Copyright 2026 Jamison A. Drapeau
#include "targets_neighbor_filter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned int assertions;
#define CHECK(TEST) do { \
        assertions++; \
        if (!(TEST)) { \
                fprintf(stderr, "FAIL: line %d: %s\n", __LINE__, #TEST); \
                free(p); \
                return 1; \
        } \
} while (0)

int main(void) {
        TARGET *p = calloc(1, sizeof(*p));
        const uint8_t fail = SCAN_RESULT_RAN | SCAN_RESULT_ERROR | SCAN_RESULT_NEIGHBOR;
        if (!p) return 1;

        CHECK(targets_prune_no_neighbor(NULL, 0, ISFALSE) == ISFALSE);
        strcpy((char *)p->IPV4, "192.0.2.1");
        CHECK(targets_prune_no_neighbor(p, 0, ISFALSE) == ISFALSE); // old/unknown format
        p->SCAN.SCAN_VERSION = TARGET_SCAN_DATA_VERSION;
        CHECK(targets_prune_no_neighbor(p, 0, ISFALSE) == ISFALSE); // unscanned
        p->SCAN.ICMPV4 = SCAN_RESULT_RAN | SCAN_RESULT_TIMEOUT;
        CHECK(targets_prune_no_neighbor(p, 0, ISFALSE) == ISFALSE); // ICMP timeout != neighbor failure
        p->SCAN.ICMPV4 = SCAN_RESULT_RAN | SCAN_RESULT_REPLY;
        CHECK(targets_prune_no_neighbor(p, 0, ISFALSE) == ISFALSE);
        p->SCAN.ICMPV4 = SCAN_RESULT_RAN | SCAN_RESULT_ERROR | SCAN_RESULT_ROUTE;
        CHECK(targets_prune_no_neighbor(p, 0, ISFALSE) == ISFALSE);
        p->SCAN.ICMPV4 = SCAN_RESULT_RAN | SCAN_RESULT_ERROR;
        CHECK(targets_prune_no_neighbor(p, 0, ISFALSE) == ISFALSE);
        p->SCAN.ICMPV4 = fail | SCAN_RESULT_REPLY;
        CHECK(targets_prune_no_neighbor(p, 0, ISFALSE) == ISFALSE); // contradictory state
        p->SCAN.ICMPV4 = fail;
        CHECK(targets_prune_no_neighbor(p, 0, ISFALSE) == ISTRUE);  // explicit v4 failure
        CHECK(targets_prune_no_neighbor(p, 0, ISTRUE) == ISFALSE);  // captured packet from target

        strcpy((char *)p->IPV6, "2001:db8::1");
        CHECK(targets_prune_no_neighbor(p, 0, ISFALSE) == ISFALSE); // untested v6
        CHECK(targets_prune_no_neighbor(p, SCAN_RESULT_RAN | SCAN_RESULT_TIMEOUT, ISFALSE) == ISFALSE);
        CHECK(targets_prune_no_neighbor(p, fail, ISFALSE) == ISTRUE); // both failed
        p->SCAN.ICMPV4 = SCAN_RESULT_RAN | SCAN_RESULT_REPLY;
        CHECK(targets_prune_no_neighbor(p, fail, ISFALSE) == ISFALSE);
        p->IPV4[0] = 0x00;
        CHECK(targets_prune_no_neighbor(p, fail, ISFALSE) == ISTRUE); // v6 only
        CHECK(targets_prune_no_neighbor(p, SCAN_RESULT_RAN | SCAN_RESULT_TIMEOUT, ISFALSE) == ISFALSE);
        p->IPV6[0] = 0x00;
        CHECK(targets_prune_no_neighbor(p, fail, ISFALSE) == ISFALSE); // no IP

        free(p);
        printf("PASS: targets del -n filter (%u assertions)\n", assertions);
        return 0;
}
