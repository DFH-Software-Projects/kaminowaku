// Copyright 2026 Jamison A. Drapeau
#ifndef __BOOK_RUNTIME__H
#define __BOOK_RUNTIME__H

#include "book_parser.h"
#include "book_session.h"
#include <stdint.h>

#define BOOK_RUNTIME_MEMORY_LIMIT          (16U * 1024U * 1024U)
#define BOOK_RUNTIME_INSTRUCTION_LIMIT     1000000ULL
#define BOOK_RUNTIME_CALL_DEPTH_LIMIT      64U
#define BOOK_RUNTIME_MULTI_LIMIT           64U
#define BOOK_RUNTIME_TABLE_ENTRY_LIMIT     65536U
#define BOOK_RUNTIME_BINDING_LIMIT         65536U
#define BOOK_RUNTIME_ERROR_BLOCK           256U

typedef enum {
        BOOK_VALUE_NIL = 0,
        BOOK_VALUE_BOOLEAN,
        BOOK_VALUE_NUMBER,
        BOOK_VALUE_STRING,
        BOOK_VALUE_TABLE,
        BOOK_VALUE_FUNCTION,
        BOOK_VALUE_NATIVE_FUNCTION
} book_value_type_t;

typedef struct BOOK_STRING BOOK_STRING;
typedef struct BOOK_TABLE BOOK_TABLE;
typedef struct BOOK_FUNCTION BOOK_FUNCTION;
typedef struct BOOK_NATIVE_FUNCTION BOOK_NATIVE_FUNCTION;
typedef struct BOOK_ENV BOOK_ENV;
typedef struct BOOK_RUNTIME BOOK_RUNTIME;

typedef struct BOOK_VALUE {
        book_value_type_t type;

        union {
                int8_t                 boolean;
                double                 number;
                BOOK_STRING *          string;
                BOOK_TABLE *           table;
                BOOK_FUNCTION *        function;
                BOOK_NATIVE_FUNCTION * native_function;
        } as;
} BOOK_VALUE;

typedef struct BOOK_MULTI_VALUE {
        BOOK_VALUE values[BOOK_RUNTIME_MULTI_LIMIT];
        c_size_t   count;
} BOOK_MULTI_VALUE;

typedef int (*book_native_callback_t)(
        BOOK_RUNTIME * runtime,
        const BOOK_VALUE * arguments,
        c_size_t argument_count,
        BOOK_MULTI_VALUE * returns
);

BOOK_RUNTIME * book_runtime_create(
        BOOK_PROGRAM * program,
        BOOK_SESSION * session
);

int book_runtime_execute(
        BOOK_RUNTIME * runtime,
        BOOK_MULTI_VALUE * returns
);

// @@ Execute one already-parsed required module inside a fresh lexical module
// scope while sharing this Book runtime's globals, limits, and native surface.
// Module execution does not require a book{} declaration.
int book_runtime_execute_module(
        BOOK_RUNTIME * runtime,
        BOOK_PROGRAM * program,
        int8_t trusted_system_module,
        BOOK_MULTI_VALUE * returns
);

BOOK_SESSION * book_runtime_session(BOOK_RUNTIME * runtime);
void * book_runtime_module_state(BOOK_RUNTIME * runtime);
void book_runtime_set_module_state(BOOK_RUNTIME * runtime, void * state);
void * book_runtime_native_state(BOOK_RUNTIME * runtime);
void book_runtime_set_native_state(BOOK_RUNTIME * runtime, void * state);

void book_runtime_destroy(BOOK_RUNTIME * runtime);

const char * book_runtime_error(const BOOK_RUNTIME * runtime);
unsigned int book_runtime_error_line(const BOOK_RUNTIME * runtime);
unsigned int book_runtime_error_column(const BOOK_RUNTIME * runtime);
uint64_t book_runtime_instruction_count(const BOOK_RUNTIME * runtime);
c_size_t book_runtime_memory_used(const BOOK_RUNTIME * runtime);

// @@ Native-library bridge used by Kaminowaku-owned standard/native modules.
// Book authors never receive direct access to these C APIs.
int book_runtime_native_fail(
        BOOK_RUNTIME * runtime,
        const char * message
);

int book_runtime_register_global(
        BOOK_RUNTIME * runtime,
        const char * name,
        book_native_callback_t callback
);

int book_runtime_set_global(
        BOOK_RUNTIME * runtime,
        const char * name,
        BOOK_VALUE value
);

int book_runtime_make_string(
        BOOK_RUNTIME * runtime,
        const unsigned char * bytes,
        c_size_t length,
        BOOK_VALUE * value
);

int book_runtime_make_table(
        BOOK_RUNTIME * runtime,
        BOOK_VALUE * value
);

int book_runtime_make_native(
        BOOK_RUNTIME * runtime,
        const char * name,
        book_native_callback_t callback,
        BOOK_VALUE * value
);

BOOK_VALUE book_runtime_nil(void);
BOOK_VALUE book_runtime_boolean(int8_t value);
BOOK_VALUE book_runtime_number(double value);

int book_runtime_truthy_value(const BOOK_VALUE * value);

int book_runtime_call_native_value(
        BOOK_RUNTIME * runtime,
        BOOK_VALUE callable,
        const BOOK_VALUE * arguments,
        c_size_t argument_count,
        BOOK_MULTI_VALUE * returns
);

int book_runtime_table_set_value(
        BOOK_RUNTIME * runtime,
        BOOK_VALUE table,
        BOOK_VALUE key,
        BOOK_VALUE value
);

int book_runtime_table_get_value(
        BOOK_RUNTIME * runtime,
        BOOK_VALUE table,
        const BOOK_VALUE * key,
        BOOK_VALUE * value
);

int book_runtime_table_set_string(
        BOOK_RUNTIME * runtime,
        BOOK_VALUE table,
        const char * key,
        BOOK_VALUE value
);

int book_runtime_table_get_string_value(
        BOOK_RUNTIME * runtime,
        BOOK_VALUE table,
        const char * key,
        BOOK_VALUE * value
);

int book_runtime_table_remove_value(
        BOOK_RUNTIME * runtime,
        BOOK_VALUE table,
        const BOOK_VALUE * key,
        BOOK_VALUE * removed
);

c_size_t book_runtime_table_length_value(const BOOK_VALUE * table);
c_size_t book_runtime_table_count_value(const BOOK_VALUE * table);

int book_runtime_table_entry_value(
        const BOOK_VALUE * table,
        c_size_t index,
        BOOK_VALUE * key,
        BOOK_VALUE * value
);

uint64_t book_runtime_random_u64(BOOK_RUNTIME * runtime);

int book_value_is_nil(const BOOK_VALUE * value);
int book_value_boolean(const BOOK_VALUE * value, int8_t * result);
int book_value_number(const BOOK_VALUE * value, double * result);
int book_value_string(
        const BOOK_VALUE * value,
        const unsigned char ** bytes,
        c_size_t * length
);

const char * book_value_type_name(book_value_type_t type);

#endif
