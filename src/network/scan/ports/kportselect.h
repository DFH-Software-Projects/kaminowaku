// Copyright 2026 Jamison A. Drapeau
#ifndef __KPORTSELECT__H
#define __KPORTSELECT__H

#include "kportspec.h"
#include <stdint.h>

// Evaluate a persisted .ports file in one pass. A match exists when any
// selected TCP port is OPEN in the latest record for either IPv4 or IPv6.
int8_t kportselect_file_has_open_tcp_ports(
        const char * PATH,
        const KPORT_SPEC * PORTS
);

#endif
