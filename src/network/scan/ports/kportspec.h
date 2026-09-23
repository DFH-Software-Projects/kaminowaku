// Copyright 2026 Jamison A. Drapeau
#ifndef __KPORTSPEC__H
#define __KPORTSPEC__H

#include <stdint.h>

// A port expression uses the scanner's existing 1..65535 range.
// 65536 bits = 8192 bytes; bit zero is deliberately never set.
#define KPORTSPEC_MAX_PORTS       65536U
#define KPORTSPEC_BITMAP_BYTES   (KPORTSPEC_MAX_PORTS / 8U)

typedef struct KPORT_SPEC {
        uint8_t  bits[KPORTSPEC_BITMAP_BYTES];
        uint32_t count; // Number of distinct selected ports.
} KPORT_SPEC;

// Replace SPEC with a parsed expression on success. On failure, leave it unchanged.
// Return 0 on success and -1 on invalid input/allocation failure.
int kportspec_parse(const char * EXPRESSION, KPORT_SPEC * SPEC);

// Add another expression to SPEC (set union), preserving it on failure.
// SPEC must have been zero-initialized or produced by kportspec_parse().
int kportspec_extend(const char * EXPRESSION, KPORT_SPEC * SPEC);

// Return 1 if PORT is selected, or 0 for an absent/invalid port or NULL SPEC.
int8_t kportspec_contains(const KPORT_SPEC * SPEC, unsigned int PORT);

#endif
