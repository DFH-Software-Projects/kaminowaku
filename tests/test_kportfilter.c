// Copyright 2026 Jamison A. Drapeau
#include "kportfilter.h"

#include <stdio.h>

static unsigned int assertions = 0U;

#define CHECK(C) do {                                                    \
        assertions++;                                                    \
        if (!(C)) {                                                      \
                fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #C); \
                return 1;                                               \
        }                                                               \
} while (0)

int main(void) {
        KPORT_SPEC ports = {0};
        KPORT_SPEC empty = {0};

        // An absent filter preserves unfiltered TCP/UDP target-context display.
        CHECK(kportfilter_slot_included(NULL, 0U, 1U) == 1);
        CHECK(kportfilter_slot_included(NULL, 1U, 65535U) == 1);
        CHECK(kportfilter_slot_included(NULL, 0U, 0U) == 0);
        CHECK(kportfilter_slot_included(NULL, 1U, 65536U) == 0);
        CHECK(kportfilter_slot_included(NULL, 2U, 80U) == 0);

        CHECK(kportspec_parse("22,80-82,443", &ports) == 0);
        CHECK(ports.count == 5U);

        // Selected TCP observations are eligible regardless of their state.
        // The renderer's DETAIL_OPEN mode separately limits detail to OPEN.
        CHECK(kportfilter_slot_included(&ports, 0U, 22U) == 1);
        CHECK(kportfilter_slot_included(&ports, 0U, 80U) == 1);
        CHECK(kportfilter_slot_included(&ports, 0U, 81U) == 1);
        CHECK(kportfilter_slot_included(&ports, 0U, 82U) == 1);
        CHECK(kportfilter_slot_included(&ports, 0U, 443U) == 1);
        CHECK(kportfilter_slot_included(&ports, 0U, 23U) == 0);
        CHECK(kportfilter_slot_included(&ports, 0U, 83U) == 0);

        // A TCP port filter must not leak UDP observations.
        CHECK(kportfilter_slot_included(&ports, 1U, 22U) == 0);
        CHECK(kportfilter_slot_included(&ports, 1U, 443U) == 0);
        CHECK(kportfilter_slot_included(&ports, 0U, 0U) == 0);
        CHECK(kportfilter_slot_included(&ports, 0U, 65536U) == 0);
        CHECK(kportfilter_slot_included(&empty, 0U, 22U) == 0);

        CHECK(kportspec_parse("1-65535", &ports) == 0);
        CHECK(ports.count == 65535U);
        CHECK(kportfilter_slot_included(&ports, 0U, 1U) == 1);
        CHECK(kportfilter_slot_included(&ports, 0U, 65535U) == 1);
        CHECK(kportfilter_slot_included(&ports, 1U, 65535U) == 0);

        printf("PASS: port rendering filter (%u assertions)\n", assertions);
        return 0;
}
