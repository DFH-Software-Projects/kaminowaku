// Copyright 2026 Jamison A. Drapeau
#ifndef __BOOK_TLS__H
#define __BOOK_TLS__H

#include "book_session.h"

typedef struct BOOK_TLS BOOK_TLS;

typedef enum {
        BOOK_TLS_OK      = 0,
        BOOK_TLS_TIMEOUT = 1,
        BOOK_TLS_EOF     = 2,
        BOOK_TLS_RESET   = 3,
        BOOK_TLS_ERROR   = 4
} book_tls_status_t;

int book_tls_open(
        BOOK_SESSION * session,
        BOOK_TLS ** tls,
        const char * server_name,
        int32_t timeout_ms
);

int book_tls_set_timeout(
        BOOK_TLS * tls,
        int32_t timeout_ms
);

book_tls_status_t book_tls_write(
        BOOK_TLS * tls,
        const unsigned char * data,
        c_size_t length,
        c_size_t * written
);

book_tls_status_t book_tls_read(
        BOOK_TLS * tls,
        unsigned char * data,
        c_size_t capacity,
        c_size_t * received
);

void book_tls_close(BOOK_TLS ** tls);

#endif
