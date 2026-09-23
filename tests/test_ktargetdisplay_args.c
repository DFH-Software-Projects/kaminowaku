// Copyright 2026 Jamison A. Drapeau
#include "ktargetdisplay_args.h"

#include <stdio.h>
#include <string.h>

static unsigned int assertions = 0;

#define CHECK(CONDITION) do {                                                     \
        assertions++;                                                             \
        if (!(CONDITION)) {                                                        \
                fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #CONDITION); \
                return 1;                                                         \
        }                                                                         \
} while (0)
#define TOKEN(S) ((unsigned char *)(S))
#define ARG_TEST(V) ktargetdisplay_parse_args((V), sizeof(V)/sizeof((V)[0]), &args)

int main(void) {
        KTARGETDISPLAY_ARGS args = {0};
        KTARGETDISPLAY_ARGS snapshot;
        unsigned char *basic[] = {TOKEN("targets"), TOKEN("display")};
        unsigned char *d[] = {TOKEN("targets"), TOKEN("display"), TOKEN("-d")};
        unsigned char *o[] = {TOKEN("targets"), TOKEN("display"), TOKEN("-o")};
        unsigned char *p1[] = {TOKEN("targets"), TOKEN("display"), TOKEN("-p"), TOKEN("443")};
        unsigned char *b1[] = {TOKEN("targets"), TOKEN("display"), TOKEN("-b"), TOKEN("65535")};
        unsigned char *range[] = {TOKEN("targets"), TOKEN("display"), TOKEN("-p"), TOKEN("20-25")};
        unsigned char *mixed[] = {TOKEN("targets"), TOKEN("display"), TOKEN("-b"), TOKEN("22,80"), TOKEN("80-82"), TOKEN("443")};
        unsigned char *overlap[] = {TOKEN("targets"), TOKEN("display"), TOKEN("-p"), TOKEN("1-65535")};
        unsigned char *legacy_commas[] = {TOKEN("targets"), TOKEN("display"), TOKEN("-p"), TOKEN(",22,,80,")};
        unsigned char *bad[][6] = {
                {TOKEN("targets"), TOKEN("display"), TOKEN("-p"), NULL},
                {TOKEN("targets"), TOKEN("display"), TOKEN("-b"), TOKEN("0"), NULL},
                {TOKEN("targets"), TOKEN("display"), TOKEN("-p"), TOKEN("22"), TOKEN("65536"), NULL},
                {TOKEN("targets"), TOKEN("display"), TOKEN("-b"), TOKEN("1-65536"), NULL},
                {TOKEN("targets"), TOKEN("display"), TOKEN("-p"), TOKEN("90-80"), NULL},
                {TOKEN("targets"), TOKEN("display"), TOKEN("-d"), TOKEN("22"), NULL},
                {TOKEN("targets"), TOKEN("display"), TOKEN("-o"), TOKEN("22"), NULL},
                {TOKEN("targets"), TOKEN("display"), TOKEN("-q"), TOKEN("22"), NULL},
                {TOKEN("targets"), TOKEN("display"), TOKEN("-b"), TOKEN("22"), TOKEN("x"), NULL},
                {TOKEN("targets"), TOKEN("display"), TOKEN("-p"), NULL, TOKEN("80"), NULL}
        };
        const size_t bad_count[] = {3,4,5,4,4,4,4,4,5,5};

        CHECK(ARG_TEST(basic) == 0);
        CHECK(args.MODE == KTARGETDISPLAY_ARG_COMPACT);
        CHECK(args.PORTS.count == 0);
        CHECK(ARG_TEST(d) == 0);
        CHECK(args.MODE == KTARGETDISPLAY_ARG_DETAIL_ALL);
        CHECK(ARG_TEST(o) == 0);
        CHECK(args.MODE == KTARGETDISPLAY_ARG_OBSERVED);
        CHECK(ARG_TEST(p1) == 0);
        CHECK(args.MODE == KTARGETDISPLAY_ARG_PORT_COMPACT);
        CHECK(args.PORTS.count == 1);
        CHECK(kportspec_contains(&args.PORTS,443) == 1);
        CHECK(ARG_TEST(b1) == 0);
        CHECK(args.MODE == KTARGETDISPLAY_ARG_PORT_DETAIL_OPEN);
        CHECK(args.PORTS.count == 1);
        CHECK(kportspec_contains(&args.PORTS,65535) == 1);
        CHECK(ARG_TEST(range) == 0);
        CHECK(args.PORTS.count == 6);
        CHECK(kportspec_contains(&args.PORTS,20) == 1);
        CHECK(kportspec_contains(&args.PORTS,25) == 1);
        CHECK(kportspec_contains(&args.PORTS,26) == 0);
        CHECK(ARG_TEST(mixed) == 0);
        CHECK(args.MODE == KTARGETDISPLAY_ARG_PORT_DETAIL_OPEN);
        CHECK(args.PORTS.count == 5);
        CHECK(kportspec_contains(&args.PORTS,22) == 1);
        CHECK(kportspec_contains(&args.PORTS,80) == 1);
        CHECK(kportspec_contains(&args.PORTS,81) == 1);
        CHECK(kportspec_contains(&args.PORTS,82) == 1);
        CHECK(kportspec_contains(&args.PORTS,443) == 1);
        CHECK(ARG_TEST(overlap) == 0);
        CHECK(args.PORTS.count == 65535);
        CHECK(ARG_TEST(legacy_commas) == 0);
        CHECK(args.PORTS.count == 2);

        snapshot = args;
        for (size_t i = 0; i < sizeof(bad)/sizeof(bad[0]); i++) {
                CHECK(ktargetdisplay_parse_args(bad[i], bad_count[i], &args) == -1);
                CHECK(memcmp(&args,&snapshot,sizeof(args)) == 0);
        }
        CHECK(ktargetdisplay_parse_args(NULL,2,&args) == -1);
        CHECK(ktargetdisplay_parse_args(basic,0,&args) == -1);
        CHECK(ktargetdisplay_parse_args(basic,2,NULL) == -1);
        CHECK(memcmp(&args,&snapshot,sizeof(args)) == 0);
        puts("PASS: project display argument parsing");
        printf("PASS: %u assertions\n", assertions);
        return 0;
}
