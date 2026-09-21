// Copyright 2026 Jamison A. Drapeau
#ifndef __BOOK_LEXER__H
#define __BOOK_LEXER__H

#include "book_core.h"

#define BOOK_LEXER_SOURCE_LIMIT (1024U * 1024U)
#define BOOK_LEXER_TOKEN_LIMIT  262144U
#define BOOK_LEXER_ERROR_BLOCK  256U

// @@ Tokens required by BOOK_LANGUAGE_V1.md.
// Supported keywords receive dedicated tokens. Lua keywords outside the v1
// subset are rejected by the lexer so they cannot silently become identifiers.
typedef enum {
        BOOK_TOKEN_EOF = 0,

        BOOK_TOKEN_IDENTIFIER,
        BOOK_TOKEN_NUMBER,
        BOOK_TOKEN_STRING,

        BOOK_TOKEN_AND,
        BOOK_TOKEN_BREAK,
        BOOK_TOKEN_DO,
        BOOK_TOKEN_ELSE,
        BOOK_TOKEN_ELSEIF,
        BOOK_TOKEN_END,
        BOOK_TOKEN_FALSE,
        BOOK_TOKEN_FOR,
        BOOK_TOKEN_FUNCTION,
        BOOK_TOKEN_IF,
        BOOK_TOKEN_IN,
        BOOK_TOKEN_LOCAL,
        BOOK_TOKEN_NIL,
        BOOK_TOKEN_NOT,
        BOOK_TOKEN_OR,
        BOOK_TOKEN_RETURN,
        BOOK_TOKEN_THEN,
        BOOK_TOKEN_TRUE,
        BOOK_TOKEN_WHILE,

        BOOK_TOKEN_PLUS,
        BOOK_TOKEN_MINUS,
        BOOK_TOKEN_STAR,
        BOOK_TOKEN_SLASH,
        BOOK_TOKEN_FLOOR_DIV,
        BOOK_TOKEN_PERCENT,
        BOOK_TOKEN_CONCAT,
        BOOK_TOKEN_LENGTH,
        BOOK_TOKEN_TILDE,
        BOOK_TOKEN_EQUAL,
        BOOK_TOKEN_EQUAL_EQUAL,
        BOOK_TOKEN_NOT_EQUAL,
        BOOK_TOKEN_LESS,
        BOOK_TOKEN_LESS_EQUAL,
        BOOK_TOKEN_GREATER,
        BOOK_TOKEN_GREATER_EQUAL,
        BOOK_TOKEN_BIT_AND,
        BOOK_TOKEN_BIT_OR,
        BOOK_TOKEN_SHIFT_LEFT,
        BOOK_TOKEN_SHIFT_RIGHT,

        BOOK_TOKEN_LEFT_PAREN,
        BOOK_TOKEN_RIGHT_PAREN,
        BOOK_TOKEN_LEFT_BRACE,
        BOOK_TOKEN_RIGHT_BRACE,
        BOOK_TOKEN_LEFT_BRACKET,
        BOOK_TOKEN_RIGHT_BRACKET,
        BOOK_TOKEN_COMMA,
        BOOK_TOKEN_DOT,
        BOOK_TOKEN_COLON,
        BOOK_TOKEN_SEMICOLON,
        BOOK_TOKEN_ELLIPSIS
} book_token_kind_t;

typedef struct BOOK_TOKEN {
        book_token_kind_t kind;
        c_size_t          offset;
        c_size_t          length;
        unsigned int      line;
        unsigned int      column;
} BOOK_TOKEN;

// @@ The lexer owns both the copied source and token array.
// Token offsets always refer to result->source.
typedef struct BOOK_LEXER_RESULT {
        unsigned char * source;
        c_size_t        source_length;

        BOOK_TOKEN *    tokens;
        c_size_t        count;
        c_size_t        capacity;

        int             status;
        unsigned int    error_line;
        unsigned int    error_column;
        char            error[BOOK_LEXER_ERROR_BLOCK];
} BOOK_LEXER_RESULT;

int book_lexer_tokenize(
        const unsigned char * source,
        c_size_t source_length,
        BOOK_LEXER_RESULT * result
);

int book_lexer_tokenize_file(
        const unsigned char * path,
        BOOK_LEXER_RESULT * result
);

void book_lexer_free(BOOK_LEXER_RESULT * result);

const char * book_token_kind_name(book_token_kind_t kind);

// @@ Decode one validated BOOK_TOKEN_STRING into caller-owned storage.
// output_length is authoritative because decoded strings may contain 0x00.
int book_lexer_decode_string(
        const BOOK_LEXER_RESULT * result,
        const BOOK_TOKEN * token,
        unsigned char * output,
        c_size_t output_size,
        c_size_t * output_length
);

#endif
