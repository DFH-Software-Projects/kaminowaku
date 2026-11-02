// Copyright 2026 Jamison A. Drapeau
#ifndef __BOOK_HELP__H
#define __BOOK_HELP__H

#include <stddef.h>

#define BOOK_HELP_SUMMARY_BLOCK    256U
#define BOOK_HELP_OPTION_BLOCK     256U
#define BOOK_HELP_OPTION_LIMIT     16U
#define BOOK_HELP_NAME_BLOCK       64U

typedef struct BOOK_HELP_OPTION {
        char name[BOOK_HELP_NAME_BLOCK];
        char description[BOOK_HELP_OPTION_BLOCK];
} BOOK_HELP_OPTION;

typedef struct BOOK_HELP {
        char description[BOOK_HELP_SUMMARY_BLOCK];
        BOOK_HELP_OPTION options[BOOK_HELP_OPTION_LIMIT];
        size_t option_count;
} BOOK_HELP;

// Read only literal fields of the top-level book { ... } declaration.
// Never instantiate the Lua runtime, execute code, open a network session,
// create a PCAP, or write target state. Returns 0 on valid metadata,
// -1 if none is available or its declaration is invalid.
int book_help_read(
        const unsigned char * path,
        const char * expected_name,
        BOOK_HELP * help
);

#endif
