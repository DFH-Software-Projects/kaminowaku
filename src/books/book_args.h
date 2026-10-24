// Copyright 2026 Jamison A. Drapeau
#ifndef __BOOK_ARGS__H
#define __BOOK_ARGS__H

#include <stddef.h>

// @@ One invocation's bounded named parameters. Values reach Lua as strings.
#define BOOK_ARGS_LIMIT       16U
#define BOOK_ARG_NAME_BLOCK   64U
#define BOOK_ARG_VALUE_BLOCK  512U

typedef struct BOOK_ARGUMENT {
        char name[BOOK_ARG_NAME_BLOCK];
        char value[BOOK_ARG_VALUE_BLOCK];
} BOOK_ARGUMENT;

typedef struct BOOK_ARGUMENTS {
        BOOK_ARGUMENT items[BOOK_ARGS_LIMIT];
        size_t count;
} BOOK_ARGUMENTS;

// Parse the original command (not the generic whitespace-only cmd_tokens).
// Accepted forms: --key value, --key=value, and quoted values.
// Returns 0 on success and -1 on invalid input, before any capture starts.
int book_args_parse(
        const char * command,
        const char * expected_book_name,
        BOOK_ARGUMENTS * arguments,
        char * error,
        size_t error_size
);

#endif
