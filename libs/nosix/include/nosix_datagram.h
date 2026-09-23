// Copyright 2026 Jamison A. Drapeau
#ifndef NOSIX_DATAGRAM_H
#define NOSIX_DATAGRAM_H

#include "nosix.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * One fixed-peer UDP transport owned by NOSIX.
 *
 * One write call emits exactly one datagram. One read call consumes exactly
 * one datagram. NOSIX_TRUNCATED explicitly reports a datagram larger than the
 * caller-provided receive buffer. The connected UDP socket also constrains
 * inbound delivery to the configured peer address/port.
 */
typedef struct nosix_datagram nosix_datagram_t;

nosix_status_t nosix_datagram_open(
        nosix_t *net,
        nosix_datagram_t **datagram,
        const nosix_address_t *destination,
        uint16_t port,
        int32_t timeout_ms
);

nosix_status_t nosix_datagram_write(
        nosix_datagram_t *datagram,
        const uint8_t *data,
        size_t length,
        size_t *written,
        int32_t timeout_ms
);

nosix_status_t nosix_datagram_read(
        nosix_datagram_t *datagram,
        uint8_t *data,
        size_t capacity,
        size_t *received,
        int32_t timeout_ms
);

nosix_status_t nosix_datagram_close(
        nosix_datagram_t **datagram
);

#ifdef __cplusplus
}
#endif

#endif
