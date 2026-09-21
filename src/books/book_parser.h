// Copyright 2026 Jamison A. Drapeau
#ifndef __BOOK_PARSER__H
#define __BOOK_PARSER__H

#include "book_lexer.h"
#include <stdint.h>

#define BOOK_PARSER_NODE_LIMIT   131072U
#define BOOK_PARSER_LIST_LIMIT   262144U
#define BOOK_PARSER_ERROR_BLOCK  256U

#define BOOK_AST_FUNCTION_VARARG  (1U << 0)

typedef enum {
        BOOK_AST_NONE = 0,

        // Expressions
        BOOK_AST_NIL,
        BOOK_AST_BOOLEAN,
        BOOK_AST_NUMBER,
        BOOK_AST_STRING,
        BOOK_AST_VARIABLE,
        BOOK_AST_VARARG,
        BOOK_AST_TABLE,
        BOOK_AST_TABLE_FIELD,
        BOOK_AST_FUNCTION,
        BOOK_AST_UNARY,
        BOOK_AST_BINARY,
        BOOK_AST_FIELD,
        BOOK_AST_INDEX,
        BOOK_AST_CALL,

        // Statements
        BOOK_AST_EXPR_STATEMENT,
        BOOK_AST_ASSIGN,
        BOOK_AST_LOCAL_ASSIGN,
        BOOK_AST_IF,
        BOOK_AST_IF_CLAUSE,
        BOOK_AST_WHILE,
        BOOK_AST_FOR_NUMERIC,
        BOOK_AST_FOR_GENERIC,
        BOOK_AST_BREAK,
        BOOK_AST_RETURN,
        BOOK_AST_FUNCTION_DECL,
        BOOK_AST_LOCAL_FUNCTION
} book_ast_kind_t;

typedef enum {
        BOOK_TABLE_FIELD_ARRAY = 0,
        BOOK_TABLE_FIELD_NAMED,
        BOOK_TABLE_FIELD_INDEXED
} book_table_field_kind_t;

// @@ Compact, index-based AST. Node/list index 0 always means "none".
// This avoids pointer invalidation when bounded backing arrays grow.
typedef struct BOOK_AST_NODE {
        book_ast_kind_t    kind;
        book_token_kind_t  op;
        c_size_t           token;

        uint32_t           a;
        uint32_t           b;
        uint32_t           c;
        uint32_t           d;

        uint32_t           list1;
        uint32_t           list2;
        uint32_t           next;

        uint32_t           flags;
} BOOK_AST_NODE;

typedef struct BOOK_AST_LIST_ENTRY {
        uint32_t node;
        uint32_t next;
} BOOK_AST_LIST_ENTRY;

typedef struct BOOK_PROGRAM {
        BOOK_LEXER_RESULT  lexer;

        BOOK_AST_NODE *    nodes;
        uint32_t           node_count;
        uint32_t           node_capacity;

        BOOK_AST_LIST_ENTRY * lists;
        uint32_t              list_count;
        uint32_t              list_capacity;

        uint32_t           root;

        int                status;
        unsigned int       error_line;
        unsigned int       error_column;
        char               error[BOOK_PARSER_ERROR_BLOCK];
} BOOK_PROGRAM;

// @@ book_parser_parse() takes ownership of lexer on entry. The caller must
// release the resulting lexer/AST together through book_program_free().
int book_parser_parse(
        BOOK_LEXER_RESULT * lexer,
        BOOK_PROGRAM * program
);

int book_parser_parse_file(
        const unsigned char * path,
        BOOK_PROGRAM * program
);

void book_program_free(BOOK_PROGRAM * program);

const BOOK_AST_NODE * book_program_node(
        const BOOK_PROGRAM * program,
        uint32_t node_id
);

const BOOK_AST_LIST_ENTRY * book_program_list_entry(
        const BOOK_PROGRAM * program,
        uint32_t list_id
);

const char * book_ast_kind_name(book_ast_kind_t kind);

#endif
