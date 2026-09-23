// Copyright 2026 Jamison A. Drapeau
#ifndef __KPORTFILTER__H
#define __KPORTFILTER__H

#include "kportspec.h"
#include <stdint.h>

// Protocol index 0 is TCP and 1 is UDP, as in kportdisplay.c.
// NULL FILTER means that every valid port observation may be rendered.
// A non-NULL FILTER limits display to its selected TCP ports.
int8_t kportfilter_slot_included(
        const KPORT_SPEC * FILTER,
        unsigned int PROTOCOL_INDEX,
        unsigned int PORT
);

#endif
