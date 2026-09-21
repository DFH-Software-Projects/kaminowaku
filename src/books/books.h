// Copyright 2026 Jamison A. Drapeau
#ifndef __BOOKS__H
#define __BOOKS__H

#include "book_core.h"

// @@ Book lookup locations
typedef enum {
        BOOK_NOT_FOUND = 0,
        BOOK_SYSTEM    = 1,
        BOOK_USER      = 2
} book_location_t;

// @@ Book command helpers
void books_print_usage(void);
book_location_t books_resolve(
        const unsigned char * book_name,
        unsigned char * book_path,
        c_size_t book_path_size
);
void books_try_run(_carry_forward * _prog_data);

#endif
