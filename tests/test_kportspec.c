// Copyright 2026 Jamison A. Drapeau
// Standalone tests: no network access, NOSIX, or project data required.
#include "kportspec.h"

#include <stdio.h>
#include <string.h>

static unsigned int assertions = 0;

#define CHECK(CONDITION) do {                                                    \
        assertions++;                                                            \
        if (!(CONDITION)) {                                                       \
                fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #CONDITION); \
                return 1;                                                        \
        }                                                                        \
} while (0)

int main(void) {
        KPORT_SPEC spec = {0};
        KPORT_SPEC snapshot;
        const char * invalid[] = {
                "", ",,,", "0", "65536", "65535-65536", "90-80",
                "20-", "-20", "20-30-40", "abc", "22,80,x",
                "999999999999999999999999", "12/24"
        };

        CHECK(kportspec_parse("443", &spec) == 0);
        CHECK(spec.count == 1U);
        CHECK(kportspec_contains(&spec, 443U) == 1);
        CHECK(kportspec_contains(&spec, 22U) == 0);

        CHECK(kportspec_parse("1,65535", &spec) == 0);
        CHECK(spec.count == 2U);
        CHECK(kportspec_contains(&spec, 1U) == 1);
        CHECK(kportspec_contains(&spec, 65535U) == 1);
        CHECK(kportspec_contains(&spec, 0U) == 0);
        CHECK(kportspec_contains(&spec, 65536U) == 0);
        CHECK(kportspec_contains(NULL, 443U) == 0);

        CHECK(kportspec_parse("22,80,443,8000-8100", &spec) == 0);
        CHECK(spec.count == 104U);
        CHECK(kportspec_contains(&spec, 22U) == 1);
        CHECK(kportspec_contains(&spec, 8000U) == 1);
        CHECK(kportspec_contains(&spec, 8050U) == 1);
        CHECK(kportspec_contains(&spec, 8100U) == 1);
        CHECK(kportspec_contains(&spec, 8101U) == 0);

        CHECK(kportspec_parse("20-30,25-35,22,35", &spec) == 0);
        CHECK(spec.count == 16U);
        CHECK(kportspec_extend("30-40,80", &spec) == 0);
        CHECK(spec.count == 22U);
        CHECK(kportspec_contains(&spec, 40U) == 1);
        CHECK(kportspec_contains(&spec, 80U) == 1);
        CHECK(kportspec_extend("80", &spec) == 0);
        CHECK(spec.count == 22U);

        CHECK(kportspec_parse("1-65535", &spec) == 0);
        CHECK(spec.count == 65535U);
        CHECK(kportspec_contains(&spec, 65535U) == 1);
        CHECK(kportspec_contains(&spec, 0U) == 0);

        // Preserve scanner v0.1.2 grammar: strtok_r() ignores empty comma fields.
        CHECK(kportspec_parse(",22,,80,", &spec) == 0);
        CHECK(spec.count == 2U);

        // A failing parse or extension must not partially replace valid state.
        snapshot = spec;
        for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
                CHECK(kportspec_parse(invalid[i], &spec) == -1);
                CHECK(memcmp(&spec, &snapshot, sizeof(spec)) == 0);
                CHECK(kportspec_extend(invalid[i], &spec) == -1);
                CHECK(memcmp(&spec, &snapshot, sizeof(spec)) == 0);
        }
        CHECK(kportspec_parse(NULL, &spec) == -1);
        CHECK(kportspec_parse("80", NULL) == -1);
        CHECK(kportspec_extend(NULL, &spec) == -1);
        CHECK(kportspec_extend("80", NULL) == -1);
        CHECK(memcmp(&spec, &snapshot, sizeof(spec)) == 0);

        CHECK(kportspec_parse("443", &spec) == 0);
        CHECK(spec.count == 1U); // Parse replaces; extend unions.

        printf("PASS: kportspec (%u assertions)\n", assertions);
        return 0;
}
