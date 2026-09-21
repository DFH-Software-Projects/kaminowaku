// Copyright 2026 Jamison A. Drapeau
#ifndef __BOOK_OUTPUT__H
#define __BOOK_OUTPUT__H

#include "book_session.h"

#define BOOK_OUTPUT_RECORD_LIMIT 256U
#define BOOK_OUTPUT_KEY_BLOCK 64U
#define BOOK_OUTPUT_VALUE_BLOCK MAX_BLOCK
#define BOOK_OUTPUT_STAGED_LIMIT OUT_BLOCK
#define BOOK_OUTPUT_LIMIT -2

typedef enum {
        BOOK_OUTPUT_COLOR_DEFAULT = 0,
        BOOK_OUTPUT_COLOR_GREEN   = 1,
        BOOK_OUTPUT_COLOR_RED     = 2,
        BOOK_OUTPUT_COLOR_YELLOW  = 3,
        BOOK_OUTPUT_COLOR_GREY    = 4
} book_output_color_t;

typedef struct BOOK_OUTPUT_RECORD {
        unsigned char       key[BOOK_OUTPUT_KEY_BLOCK];
        unsigned char       value[BOOK_OUTPUT_VALUE_BLOCK];
        c_size_t            value_length;
        book_output_color_t color;
} BOOK_OUTPUT_RECORD;

struct BOOK_OUTPUT_STAGE {
        BOOK_OUTPUT_RECORD * records;
        c_size_t              count;
        c_size_t              capacity;
        c_size_t              staged_bytes;
};

int book_output_parse_color(const char * color, book_output_color_t * parsed);
const char * book_output_color_name(book_output_color_t color);
int book_output_emit(
        BOOK_SESSION * session,
        const char * key,
        c_size_t key_length,
        const char * value,
        c_size_t value_length,
        book_output_color_t color
);
c_size_t book_output_count(const BOOK_SESSION * session);
void book_output_discard(BOOK_SESSION * session);

#endif
