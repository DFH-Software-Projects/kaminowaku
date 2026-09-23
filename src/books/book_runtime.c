// Copyright 2026 Jamison A. Drapeau
#include "book_runtime.h"
#include "book_stdlib.h"
#include "book_module.h"
#include "book_native.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
        BOOK_FLOW_NORMAL = 0,
        BOOK_FLOW_RETURN,
        BOOK_FLOW_BREAK,
        BOOK_FLOW_ERROR
} book_flow_t;

typedef struct BOOK_RUNTIME_ALLOCATION {
        struct BOOK_RUNTIME_ALLOCATION * next;
        c_size_t size;
} BOOK_RUNTIME_ALLOCATION;

struct BOOK_STRING {
        c_size_t length;
        unsigned char bytes[];
};

typedef struct BOOK_TABLE_ENTRY {
        BOOK_VALUE key;
        BOOK_VALUE value;
} BOOK_TABLE_ENTRY;

struct BOOK_TABLE {
        BOOK_TABLE_ENTRY * entries;
        c_size_t count;
        c_size_t capacity;
};

typedef struct BOOK_BINDING {
        BOOK_STRING * name;
        BOOK_VALUE value;
} BOOK_BINDING;

struct BOOK_ENV {
        BOOK_ENV * parent;

        BOOK_BINDING * bindings;
        c_size_t count;
        c_size_t capacity;

        int8_t function_scope;
        BOOK_VALUE * varargs;
        c_size_t vararg_count;
};

struct BOOK_FUNCTION {
        BOOK_PROGRAM * program;
        uint32_t node_id;
        BOOK_ENV * closure;
};

struct BOOK_NATIVE_FUNCTION {
        const char * name;
        book_native_callback_t callback;
};

struct BOOK_RUNTIME {
        BOOK_PROGRAM * program;
        BOOK_SESSION * session;
        BOOK_ENV * global;

        BOOK_RUNTIME_ALLOCATION * allocations;
        c_size_t memory_used;

        uint64_t instructions;
        unsigned int call_depth;

        c_size_t table_entries;
        c_size_t bindings;

        uint64_t prng_state;
        void * module_state;
        void * native_state;
        unsigned int module_execution_depth;
        int8_t declared_book;

        int status;
        unsigned int error_line;
        unsigned int error_column;
        char error[BOOK_RUNTIME_ERROR_BLOCK];
};

static BOOK_VALUE book_value_nil_value(void) {
        BOOK_VALUE value;

        memset(&value, 0x00, sizeof(value));
        value.type = BOOK_VALUE_NIL;
        return value;
}

static BOOK_VALUE book_value_boolean_value(int8_t boolean) {
        BOOK_VALUE value;

        memset(&value, 0x00, sizeof(value));
        value.type = BOOK_VALUE_BOOLEAN;
        value.as.boolean = boolean == ISTRUE ? ISTRUE : ISFALSE;
        return value;
}

static BOOK_VALUE book_value_number_value(double number) {
        BOOK_VALUE value;

        memset(&value, 0x00, sizeof(value));
        value.type = BOOK_VALUE_NUMBER;
        value.as.number = number;
        return value;
}

static BOOK_VALUE book_value_string_value(BOOK_STRING * string) {
        BOOK_VALUE value;

        memset(&value, 0x00, sizeof(value));
        value.type = BOOK_VALUE_STRING;
        value.as.string = string;
        return value;
}

static BOOK_VALUE book_value_table_value(BOOK_TABLE * table) {
        BOOK_VALUE value;

        memset(&value, 0x00, sizeof(value));
        value.type = BOOK_VALUE_TABLE;
        value.as.table = table;
        return value;
}

static BOOK_VALUE book_value_function_value(BOOK_FUNCTION * function) {
        BOOK_VALUE value;

        memset(&value, 0x00, sizeof(value));
        value.type = BOOK_VALUE_FUNCTION;
        value.as.function = function;
        return value;
}

static BOOK_VALUE book_value_native_value(BOOK_NATIVE_FUNCTION * function) {
        BOOK_VALUE value;

        memset(&value, 0x00, sizeof(value));
        value.type = BOOK_VALUE_NATIVE_FUNCTION;
        value.as.native_function = function;
        return value;
}

static void book_multi_clear(BOOK_MULTI_VALUE * values) {
        if (!values) return;
        memset(values, 0x00, sizeof(*values));
}

static int book_multi_append(
        BOOK_MULTI_VALUE * values,
        BOOK_VALUE value
) {
        if (!values) return ABNORMAL;
        if (values->count >= BOOK_RUNTIME_MULTI_LIMIT) return ABNORMAL;

        values->values[values->count++] = value;
        return NORMAL;
}

static BOOK_VALUE book_multi_first(const BOOK_MULTI_VALUE * values) {
        if (!values || values->count == 0) {
                return book_value_nil_value();
        }

        return values->values[0];
}

static const BOOK_TOKEN * book_runtime_node_token(
        const BOOK_RUNTIME * runtime,
        uint32_t node_id
) {
        const BOOK_AST_NODE * node;

        if (
                !runtime
                || !runtime->program
                || node_id == 0
                || node_id > runtime->program->node_count
        ) {
                return NULL;
        }

        node = book_program_node(runtime->program, node_id);
        if (
                !node
                || node->token >= runtime->program->lexer.count
        ) {
                return NULL;
        }

        return &runtime->program->lexer.tokens[node->token];
}

static int book_runtime_fail_token(
        BOOK_RUNTIME * runtime,
        const BOOK_TOKEN * token,
        const char * message
) {
        if (!runtime || !message) return ABNORMAL;

        if (runtime->status != ABNORMAL) {
                runtime->status = ABNORMAL;
                runtime->error_line = token ? token->line : 1;
                runtime->error_column = token ? token->column : 1;
                snprintf(
                        runtime->error,
                        sizeof(runtime->error),
                        "%s",
                        message
                );
        }

        return ABNORMAL;
}

static int book_runtime_fail_node(
        BOOK_RUNTIME * runtime,
        uint32_t node_id,
        const char * message
) {
        return book_runtime_fail_token(
                runtime,
                book_runtime_node_token(runtime, node_id),
                message
        );
}

static int book_runtime_tick(
        BOOK_RUNTIME * runtime,
        uint32_t node_id
) {
        if (!runtime) return ABNORMAL;

        // Check wall time even for scripts that never call a native receive.
        if (
                (runtime->instructions & 255ULL) == 0
                && runtime->session
                && books_session_remaining_ms(runtime->session) <= 0
        ) {
                if (runtime->session->termination == BOOK_TERM_NONE) {
                        books_session_set_termination(
                                runtime->session, BOOK_TERM_LIMIT_ERROR
                        );
                }
                return book_runtime_fail_node(
                        runtime, node_id, "Book hard execution deadline exceeded"
                );
        }

        if (runtime->instructions >= BOOK_RUNTIME_INSTRUCTION_LIMIT) {
                return book_runtime_fail_node(
                        runtime,
                        node_id,
                        "Lua instruction limit exceeded"
                );
        }

        runtime->instructions++;
        return NORMAL;
}

static void * book_runtime_alloc(
        BOOK_RUNTIME * runtime,
        c_size_t size
) {
        BOOK_RUNTIME_ALLOCATION * allocation;
        c_size_t total;

        if (!runtime || size == 0) return NULL;
        if (size > SIZE_MAX - sizeof(*allocation)) return NULL;

        total = sizeof(*allocation) + size;
        if (
                runtime->memory_used > BOOK_RUNTIME_MEMORY_LIMIT
                || total > BOOK_RUNTIME_MEMORY_LIMIT - runtime->memory_used
        ) {
                (void)book_runtime_fail_token(
                        runtime,
                        NULL,
                        "Lua runtime memory limit exceeded"
                );
                return NULL;
        }

        allocation = calloc(1, total);
        if (!allocation) {
                (void)book_runtime_fail_token(
                        runtime,
                        NULL,
                        "failed to allocate Lua runtime memory"
                );
                return NULL;
        }

        allocation->size = size;
        allocation->next = runtime->allocations;
        runtime->allocations = allocation;
        runtime->memory_used += total;

        return (void*)(allocation + 1);
}

static BOOK_STRING * book_runtime_string_new(
        BOOK_RUNTIME * runtime,
        const unsigned char * bytes,
        c_size_t length
) {
        BOOK_STRING * string;

        if (!runtime) return NULL;
        if (length > SIZE_MAX - sizeof(*string) - 1U) return NULL;

        string = book_runtime_alloc(
                runtime,
                sizeof(*string) + length + 1U
        );
        if (!string) return NULL;

        string->length = length;
        if (length > 0 && bytes) {
                memcpy(string->bytes, bytes, length);
        }
        string->bytes[length] = 0x00;
        return string;
}

static int book_runtime_string_equal(
        const BOOK_STRING * left,
        const BOOK_STRING * right
) {
        if (!left || !right) return ISFALSE;
        if (left->length != right->length) return ISFALSE;

        return (
                left->length == 0
                || memcmp(left->bytes, right->bytes, left->length) == MATCH
        ) ? ISTRUE : ISFALSE;
}

static int book_runtime_string_equals_bytes(
        const BOOK_STRING * string,
        const unsigned char * bytes,
        c_size_t length
) {
        if (!string || (!bytes && length > 0)) return ISFALSE;
        if (string->length != length) return ISFALSE;

        return (
                length == 0
                || memcmp(string->bytes, bytes, length) == MATCH
        ) ? ISTRUE : ISFALSE;
}

static int book_runtime_token_bytes(
        const BOOK_RUNTIME * runtime,
        c_size_t token_index,
        const unsigned char ** bytes,
        c_size_t * length
) {
        const BOOK_TOKEN * token;

        if (
                !runtime
                || !runtime->program
                || !bytes
                || !length
                || token_index >= runtime->program->lexer.count
        ) {
                return ABNORMAL;
        }

        token = &runtime->program->lexer.tokens[token_index];

        if (
                token->offset > runtime->program->lexer.source_length
                || token->length
                        > runtime->program->lexer.source_length - token->offset
        ) {
                return ABNORMAL;
        }

        *bytes = runtime->program->lexer.source + token->offset;
        *length = token->length;
        return NORMAL;
}

static BOOK_STRING * book_runtime_string_from_token(
        BOOK_RUNTIME * runtime,
        c_size_t token_index
) {
        const unsigned char * bytes;
        c_size_t length;

        if (
                book_runtime_token_bytes(
                        runtime,
                        token_index,
                        &bytes,
                        &length
                ) != NORMAL
        ) {
                return NULL;
        }

        return book_runtime_string_new(runtime, bytes, length);
}

static BOOK_STRING * book_runtime_decoded_string(
        BOOK_RUNTIME * runtime,
        c_size_t token_index
) {
        const BOOK_TOKEN * token;
        BOOK_STRING * string;
        c_size_t decoded_length = 0;

        if (
                !runtime
                || !runtime->program
                || token_index >= runtime->program->lexer.count
        ) {
                return NULL;
        }

        token = &runtime->program->lexer.tokens[token_index];
        if (token->kind != BOOK_TOKEN_STRING) return NULL;

        string = book_runtime_alloc(
                runtime,
                sizeof(*string) + token->length + 1U
        );
        if (!string) return NULL;

        if (
                book_lexer_decode_string(
                        &runtime->program->lexer,
                        token,
                        string->bytes,
                        token->length + 1U,
                        &decoded_length
                ) != NORMAL
        ) {
                (void)book_runtime_fail_token(
                        runtime,
                        token,
                        "failed to decode Lua string"
                );
                return NULL;
        }

        string->length = decoded_length;
        return string;
}

static BOOK_ENV * book_runtime_env_new(
        BOOK_RUNTIME * runtime,
        BOOK_ENV * parent,
        int8_t function_scope
) {
        BOOK_ENV * environment;

        environment = book_runtime_alloc(
                runtime,
                sizeof(*environment)
        );
        if (!environment) return NULL;

        environment->parent = parent;
        environment->function_scope = function_scope;
        return environment;
}

static int book_runtime_env_reserve(
        BOOK_RUNTIME * runtime,
        BOOK_ENV * environment
) {
        BOOK_BINDING * bindings;
        c_size_t next;

        if (!runtime || !environment) return ABNORMAL;
        if (environment->count < environment->capacity) return NORMAL;

        next = environment->capacity
                ? environment->capacity * 2U
                : 8U;

        if (
                next > BOOK_RUNTIME_BINDING_LIMIT
                || runtime->bindings > BOOK_RUNTIME_BINDING_LIMIT
        ) {
                return book_runtime_fail_token(
                        runtime,
                        NULL,
                        "Lua binding limit exceeded"
                );
        }

        bindings = book_runtime_alloc(
                runtime,
                next * sizeof(*bindings)
        );
        if (!bindings) return ABNORMAL;

        if (environment->count > 0) {
                memcpy(
                        bindings,
                        environment->bindings,
                        environment->count * sizeof(*bindings)
                );
        }

        environment->bindings = bindings;
        environment->capacity = next;
        return NORMAL;
}

static BOOK_BINDING * book_runtime_env_find_local_bytes(
        BOOK_ENV * environment,
        const unsigned char * name,
        c_size_t length
) {
        if (!environment || (!name && length > 0)) return NULL;

        for (c_size_t i = 0; i < environment->count; i++) {
                if (
                        book_runtime_string_equals_bytes(
                                environment->bindings[i].name,
                                name,
                                length
                        ) == ISTRUE
                ) {
                        return &environment->bindings[i];
                }
        }

        return NULL;
}

static BOOK_BINDING * book_runtime_env_find_bytes(
        BOOK_ENV * environment,
        const unsigned char * name,
        c_size_t length
) {
        BOOK_ENV * current = environment;

        while (current) {
                BOOK_BINDING * binding = book_runtime_env_find_local_bytes(
                        current,
                        name,
                        length
                );

                if (binding) return binding;
                current = current->parent;
        }

        return NULL;
}

static int book_runtime_env_define_string(
        BOOK_RUNTIME * runtime,
        BOOK_ENV * environment,
        BOOK_STRING * name,
        BOOK_VALUE value
) {
        BOOK_BINDING * existing;

        if (!runtime || !environment || !name) return ABNORMAL;

        existing = book_runtime_env_find_local_bytes(
                environment,
                name->bytes,
                name->length
        );
        if (existing) {
                existing->value = value;
                return NORMAL;
        }

        if (runtime->bindings >= BOOK_RUNTIME_BINDING_LIMIT) {
                return book_runtime_fail_token(
                        runtime,
                        NULL,
                        "Lua binding limit exceeded"
                );
        }

        if (book_runtime_env_reserve(runtime, environment) != NORMAL) {
                return ABNORMAL;
        }

        environment->bindings[environment->count].name = name;
        environment->bindings[environment->count].value = value;
        environment->count++;
        runtime->bindings++;
        return NORMAL;
}

static int book_runtime_env_define_token(
        BOOK_RUNTIME * runtime,
        BOOK_ENV * environment,
        c_size_t token_index,
        BOOK_VALUE value
) {
        BOOK_STRING * name;

        name = book_runtime_string_from_token(runtime, token_index);
        if (!name) {
                return book_runtime_fail_token(
                        runtime,
                        NULL,
                        "failed to allocate Lua identifier"
                );
        }

        return book_runtime_env_define_string(
                runtime,
                environment,
                name,
                value
        );
}

static int book_runtime_env_get_token(
        BOOK_RUNTIME * runtime,
        BOOK_ENV * environment,
        c_size_t token_index,
        BOOK_VALUE * value
) {
        const unsigned char * bytes;
        c_size_t length;
        BOOK_BINDING * binding;

        if (!runtime || !environment || !value) return ABNORMAL;

        if (
                book_runtime_token_bytes(
                        runtime,
                        token_index,
                        &bytes,
                        &length
                ) != NORMAL
        ) {
                return ABNORMAL;
        }

        binding = book_runtime_env_find_bytes(
                environment,
                bytes,
                length
        );

        *value = binding
                ? binding->value
                : book_value_nil_value();

        return NORMAL;
}

static int book_runtime_env_assign_token(
        BOOK_RUNTIME * runtime,
        BOOK_ENV * environment,
        c_size_t token_index,
        BOOK_VALUE value
) {
        const unsigned char * bytes;
        c_size_t length;
        BOOK_BINDING * binding;

        if (!runtime || !environment) return ABNORMAL;

        if (
                book_runtime_token_bytes(
                        runtime,
                        token_index,
                        &bytes,
                        &length
                ) != NORMAL
        ) {
                return ABNORMAL;
        }

        binding = book_runtime_env_find_bytes(
                environment,
                bytes,
                length
        );
        if (binding) {
                binding->value = value;
                return NORMAL;
        }

        return book_runtime_env_define_token(
                runtime,
                runtime->global,
                token_index,
                value
        );
}

static int book_runtime_number_to_integer(
        BOOK_RUNTIME * runtime,
        uint32_t node_id,
        double number,
        int64_t * integer
) {
        int64_t candidate;

        if (!integer) return ABNORMAL;
        // INT64_MAX rounds to 2^63 when represented as double. Use the exact
        // exclusive upper power-of-two boundary so the cast can never receive
        // +2^63, which is outside int64_t.
        if (
                number < -9223372036854775808.0
                || number >= 9223372036854775808.0
        ) {
                return book_runtime_fail_node(
                        runtime,
                        node_id,
                        "Lua integer operation is outside int64 range"
                );
        }

        candidate = (int64_t)number;
        if ((double)candidate != number) {
                return book_runtime_fail_node(
                        runtime,
                        node_id,
                        "Lua integer operation requires an integral number"
                );
        }

        *integer = candidate;
        return NORMAL;
}

static int book_runtime_table_key_equal(
        const BOOK_VALUE * left,
        const BOOK_VALUE * right
) {
        if (!left || !right || left->type != right->type) return ISFALSE;

        switch (left->type) {
                case BOOK_VALUE_NUMBER:
                        return left->as.number == right->as.number
                                ? ISTRUE
                                : ISFALSE;

                case BOOK_VALUE_STRING:
                        return book_runtime_string_equal(
                                left->as.string,
                                right->as.string
                        );

                default:
                        return ISFALSE;
        }
}

static int book_runtime_table_reserve(
        BOOK_RUNTIME * runtime,
        BOOK_TABLE * table
) {
        BOOK_TABLE_ENTRY * entries;
        c_size_t next;

        if (!runtime || !table) return ABNORMAL;
        if (table->count < table->capacity) return NORMAL;

        next = table->capacity
                ? table->capacity * 2U
                : 8U;

        if (next > BOOK_RUNTIME_TABLE_ENTRY_LIMIT) {
                return book_runtime_fail_token(
                        runtime,
                        NULL,
                        "Lua table entry limit exceeded"
                );
        }

        entries = book_runtime_alloc(
                runtime,
                next * sizeof(*entries)
        );
        if (!entries) return ABNORMAL;

        if (table->count > 0) {
                memcpy(
                        entries,
                        table->entries,
                        table->count * sizeof(*entries)
                );
        }

        table->entries = entries;
        table->capacity = next;
        return NORMAL;
}

static BOOK_TABLE * book_runtime_table_new(BOOK_RUNTIME * runtime) {
        if (!runtime) return NULL;

        return book_runtime_alloc(runtime, sizeof(BOOK_TABLE));
}

static int book_runtime_table_key_valid(
        BOOK_RUNTIME * runtime,
        uint32_t node_id,
        const BOOK_VALUE * key
) {
        int64_t integer;

        if (!key) return ABNORMAL;

        if (key->type == BOOK_VALUE_STRING) return NORMAL;

        if (key->type == BOOK_VALUE_NUMBER) {
                return book_runtime_number_to_integer(
                        runtime,
                        node_id,
                        key->as.number,
                        &integer
                );
        }

        return book_runtime_fail_node(
                runtime,
                node_id,
                "Lua v1 table keys must be strings or integers"
        );
}

static int book_runtime_table_set(
        BOOK_RUNTIME * runtime,
        uint32_t node_id,
        BOOK_TABLE * table,
        BOOK_VALUE key,
        BOOK_VALUE value
) {
        if (!runtime || !table) return ABNORMAL;
        if (book_runtime_table_key_valid(runtime, node_id, &key) != NORMAL) {
                return ABNORMAL;
        }

        for (c_size_t i = 0; i < table->count; i++) {
                if (
                        book_runtime_table_key_equal(
                                &table->entries[i].key,
                                &key
                        ) == ISTRUE
                ) {
                        // Lua table assignment of nil removes the key.
                        if (value.type == BOOK_VALUE_NIL) {
                                if (i + 1U < table->count) {
                                        memmove(
                                                &table->entries[i],
                                                &table->entries[i + 1U],
                                                (table->count - i - 1U)
                                                        * sizeof(*table->entries)
                                        );
                                }
                                table->count--;
                                if (runtime->table_entries > 0) {
                                        runtime->table_entries--;
                                }
                                return NORMAL;
                        }

                        table->entries[i].value = value;
                        return NORMAL;
                }
        }

        if (value.type == BOOK_VALUE_NIL) return NORMAL;

        if (runtime->table_entries >= BOOK_RUNTIME_TABLE_ENTRY_LIMIT) {
                return book_runtime_fail_node(
                        runtime,
                        node_id,
                        "Lua table entry limit exceeded"
                );
        }

        if (book_runtime_table_reserve(runtime, table) != NORMAL) {
                return ABNORMAL;
        }

        table->entries[table->count].key = key;
        table->entries[table->count].value = value;
        table->count++;
        runtime->table_entries++;
        return NORMAL;
}

static int book_runtime_table_get(
        BOOK_RUNTIME * runtime,
        uint32_t node_id,
        BOOK_TABLE * table,
        const BOOK_VALUE * key,
        BOOK_VALUE * value
) {
        if (!runtime || !table || !key || !value) return ABNORMAL;
        if (book_runtime_table_key_valid(runtime, node_id, key) != NORMAL) {
                return ABNORMAL;
        }

        for (c_size_t i = 0; i < table->count; i++) {
                if (
                        book_runtime_table_key_equal(
                                &table->entries[i].key,
                                key
                        ) == ISTRUE
                ) {
                        *value = table->entries[i].value;
                        return NORMAL;
                }
        }

        *value = book_value_nil_value();
        return NORMAL;
}

static int book_runtime_table_get_string(
        BOOK_TABLE * table,
        const unsigned char * bytes,
        c_size_t length,
        BOOK_VALUE * value
) {
        if (!table || (!bytes && length > 0) || !value) return ABNORMAL;

        for (c_size_t i = 0; i < table->count; i++) {
                if (
                        table->entries[i].key.type == BOOK_VALUE_STRING
                        && book_runtime_string_equals_bytes(
                                table->entries[i].key.as.string,
                                bytes,
                                length
                        ) == ISTRUE
                ) {
                        *value = table->entries[i].value;
                        return NORMAL;
                }
        }

        *value = book_value_nil_value();
        return NORMAL;
}

static c_size_t book_runtime_table_length(BOOK_TABLE * table) {
        c_size_t length = 0;

        if (!table) return 0;

        for (;;) {
                BOOK_VALUE key = book_value_number_value(
                        (double)(length + 1U)
                );
                BOOK_VALUE value = book_value_nil_value();
                int found = ISFALSE;

                for (c_size_t i = 0; i < table->count; i++) {
                        if (
                                book_runtime_table_key_equal(
                                        &table->entries[i].key,
                                        &key
                                ) == ISTRUE
                        ) {
                                value = table->entries[i].value;
                                found = ISTRUE;
                                break;
                        }
                }

                if (found != ISTRUE || value.type == BOOK_VALUE_NIL) break;
                length++;
        }

        return length;
}

static int book_runtime_truthy(const BOOK_VALUE * value) {
        if (!value) return ISFALSE;
        if (value->type == BOOK_VALUE_NIL) return ISFALSE;
        if (
                value->type == BOOK_VALUE_BOOLEAN
                && value->as.boolean != ISTRUE
        ) {
                return ISFALSE;
        }

        return ISTRUE;
}

static int book_runtime_value_equal(
        const BOOK_VALUE * left,
        const BOOK_VALUE * right
) {
        if (!left || !right) return ISFALSE;
        if (left->type != right->type) return ISFALSE;

        switch (left->type) {
                case BOOK_VALUE_NIL:
                        return ISTRUE;

                case BOOK_VALUE_BOOLEAN:
                        return left->as.boolean == right->as.boolean
                                ? ISTRUE
                                : ISFALSE;

                case BOOK_VALUE_NUMBER:
                        return left->as.number == right->as.number
                                ? ISTRUE
                                : ISFALSE;

                case BOOK_VALUE_STRING:
                        return book_runtime_string_equal(
                                left->as.string,
                                right->as.string
                        );

                case BOOK_VALUE_TABLE:
                        return left->as.table == right->as.table
                                ? ISTRUE
                                : ISFALSE;

                case BOOK_VALUE_FUNCTION:
                        return left->as.function == right->as.function
                                ? ISTRUE
                                : ISFALSE;

                case BOOK_VALUE_NATIVE_FUNCTION:
                        return left->as.native_function
                                == right->as.native_function
                                ? ISTRUE
                                : ISFALSE;

                default:
                        return ISFALSE;
        }
}

static int book_runtime_number_literal(
        BOOK_RUNTIME * runtime,
        uint32_t node_id,
        c_size_t token_index,
        double * number
) {
        const unsigned char * bytes;
        c_size_t length;
        char buffer[128];
        char * end = NULL;

        if (!runtime || !number) return ABNORMAL;

        if (
                book_runtime_token_bytes(
                        runtime,
                        token_index,
                        &bytes,
                        &length
                ) != NORMAL
        ) {
                return book_runtime_fail_node(
                        runtime,
                        node_id,
                        "invalid numeric token"
                );
        }

        if (length == 0 || length >= sizeof(buffer)) {
                return book_runtime_fail_node(
                        runtime,
                        node_id,
                        "numeric literal is too long"
                );
        }

        memcpy(buffer, bytes, length);
        buffer[length] = 0x00;

        errno = 0;

        if (
                length >= 2
                && buffer[0] == '0'
                && (buffer[1] == 'x' || buffer[1] == 'X')
        ) {
                unsigned long long integer = strtoull(
                        buffer,
                        &end,
                        0
                );

                if (
                        errno != 0
                        || !end
                        || *end != 0x00
                ) {
                        return book_runtime_fail_node(
                                runtime,
                                node_id,
                                "invalid hexadecimal number"
                        );
                }

                *number = (double)integer;
                return NORMAL;
        }

        *number = strtod(buffer, &end);
        if (
                errno != 0
                || !end
                || *end != 0x00
        ) {
                return book_runtime_fail_node(
                        runtime,
                        node_id,
                        "invalid decimal number"
                );
        }

        return NORMAL;
}

static int book_runtime_value_to_concat(
        BOOK_RUNTIME * runtime,
        uint32_t node_id,
        BOOK_VALUE value,
        const unsigned char ** bytes,
        c_size_t * length,
        char number_buffer[64]
) {
        int written;

        if (!runtime || !bytes || !length || !number_buffer) return ABNORMAL;

        if (value.type == BOOK_VALUE_STRING && value.as.string) {
                *bytes = value.as.string->bytes;
                *length = value.as.string->length;
                return NORMAL;
        }

        if (value.type == BOOK_VALUE_NUMBER) {
                written = snprintf(
                        number_buffer,
                        64,
                        "%.14g",
                        value.as.number
                );

                if (written <= 0 || written >= 64) {
                        return book_runtime_fail_node(
                                runtime,
                                node_id,
                                "failed to format number for concatenation"
                        );
                }

                *bytes = (const unsigned char*)number_buffer;
                *length = (c_size_t)written;
                return NORMAL;
        }

        return book_runtime_fail_node(
                runtime,
                node_id,
                "Lua concatenation requires strings or numbers"
        );
}

static int book_runtime_eval(
        BOOK_RUNTIME * runtime,
        BOOK_ENV * environment,
        uint32_t node_id,
        BOOK_MULTI_VALUE * values
);

static book_flow_t book_runtime_exec_block(
        BOOK_RUNTIME * runtime,
        BOOK_ENV * environment,
        uint32_t first_statement,
        BOOK_MULTI_VALUE * returns
);

static int book_runtime_eval_list(
        BOOK_RUNTIME * runtime,
        BOOK_ENV * environment,
        uint32_t list_id,
        BOOK_MULTI_VALUE * values
) {
        uint32_t current = list_id;

        if (!runtime || !environment || !values) return ABNORMAL;
        book_multi_clear(values);

        while (current) {
                const BOOK_AST_LIST_ENTRY * entry;
                BOOK_MULTI_VALUE evaluated;
                int8_t final;

                entry = book_program_list_entry(
                        runtime->program,
                        current
                );
                if (!entry) {
                        return book_runtime_fail_token(
                                runtime,
                                NULL,
                                "invalid AST list entry"
                        );
                }

                final = entry->next == 0 ? ISTRUE : ISFALSE;
                book_multi_clear(&evaluated);

                if (
                        book_runtime_eval(
                                runtime,
                                environment,
                                entry->node,
                                &evaluated
                        ) != NORMAL
                ) {
                        return ABNORMAL;
                }

                if (final == ISTRUE) {
                        for (c_size_t i = 0; i < evaluated.count; i++) {
                                if (
                                        book_multi_append(
                                                values,
                                                evaluated.values[i]
                                        ) != NORMAL
                                ) {
                                        return book_runtime_fail_node(
                                                runtime,
                                                entry->node,
                                                "Lua multiple-value limit exceeded"
                                        );
                                }
                        }
                } else {
                        if (
                                book_multi_append(
                                        values,
                                        book_multi_first(&evaluated)
                                ) != NORMAL
                        ) {
                                return book_runtime_fail_node(
                                        runtime,
                                        entry->node,
                                        "Lua multiple-value limit exceeded"
                                );
                        }
                }

                current = entry->next;
        }

        return NORMAL;
}

static int book_runtime_call_value(
        BOOK_RUNTIME * runtime,
        uint32_t call_node_id,
        BOOK_VALUE callable,
        const BOOK_VALUE * arguments,
        c_size_t argument_count,
        BOOK_MULTI_VALUE * returns
);

static int book_runtime_eval_table(
        BOOK_RUNTIME * runtime,
        BOOK_ENV * environment,
        uint32_t node_id,
        const BOOK_AST_NODE * node,
        BOOK_MULTI_VALUE * values
) {
        BOOK_TABLE * table;
        uint32_t current;
        uint64_t next_array_index = 1;

        if (!runtime || !environment || !node || !values) return ABNORMAL;

        table = book_runtime_table_new(runtime);
        if (!table) return ABNORMAL;

        current = node->list1;

        while (current) {
                const BOOK_AST_LIST_ENTRY * list_entry;
                const BOOK_AST_NODE * field;
                BOOK_MULTI_VALUE evaluated;
                BOOK_VALUE key;
                int8_t final;

                list_entry = book_program_list_entry(
                        runtime->program,
                        current
                );
                if (!list_entry) {
                        return book_runtime_fail_node(
                                runtime,
                                node_id,
                                "invalid table field list"
                        );
                }

                field = book_program_node(
                        runtime->program,
                        list_entry->node
                );
                if (!field || field->kind != BOOK_AST_TABLE_FIELD) {
                        return book_runtime_fail_node(
                                runtime,
                                node_id,
                                "invalid table field"
                        );
                }

                final = list_entry->next == 0 ? ISTRUE : ISFALSE;
                book_multi_clear(&evaluated);

                if (field->flags == BOOK_TABLE_FIELD_NAMED) {
                        BOOK_STRING * name = book_runtime_string_from_token(
                                runtime,
                                field->token
                        );

                        if (!name) return ABNORMAL;
                        key = book_value_string_value(name);

                        if (
                                book_runtime_eval(
                                        runtime,
                                        environment,
                                        field->b,
                                        &evaluated
                                ) != NORMAL
                        ) {
                                return ABNORMAL;
                        }

                        if (
                                book_runtime_table_set(
                                        runtime,
                                        list_entry->node,
                                        table,
                                        key,
                                        book_multi_first(&evaluated)
                                ) != NORMAL
                        ) {
                                return ABNORMAL;
                        }
                } else if (field->flags == BOOK_TABLE_FIELD_INDEXED) {
                        BOOK_MULTI_VALUE key_values;

                        book_multi_clear(&key_values);
                        if (
                                book_runtime_eval(
                                        runtime,
                                        environment,
                                        field->a,
                                        &key_values
                                ) != NORMAL
                        ) {
                                return ABNORMAL;
                        }

                        if (
                                book_runtime_eval(
                                        runtime,
                                        environment,
                                        field->b,
                                        &evaluated
                                ) != NORMAL
                        ) {
                                return ABNORMAL;
                        }

                        if (
                                book_runtime_table_set(
                                        runtime,
                                        list_entry->node,
                                        table,
                                        book_multi_first(&key_values),
                                        book_multi_first(&evaluated)
                                ) != NORMAL
                        ) {
                                return ABNORMAL;
                        }
                } else {
                        if (
                                book_runtime_eval(
                                        runtime,
                                        environment,
                                        field->b,
                                        &evaluated
                                ) != NORMAL
                        ) {
                                return ABNORMAL;
                        }

                        if (final == ISTRUE) {
                                for (c_size_t i = 0; i < evaluated.count; i++) {
                                        key = book_value_number_value(
                                                (double)next_array_index++
                                        );

                                        if (
                                                book_runtime_table_set(
                                                        runtime,
                                                        list_entry->node,
                                                        table,
                                                        key,
                                                        evaluated.values[i]
                                                ) != NORMAL
                                        ) {
                                                return ABNORMAL;
                                        }
                                }
                        } else {
                                key = book_value_number_value(
                                        (double)next_array_index++
                                );

                                if (
                                        book_runtime_table_set(
                                                runtime,
                                                list_entry->node,
                                                table,
                                                key,
                                                book_multi_first(&evaluated)
                                        ) != NORMAL
                                ) {
                                        return ABNORMAL;
                                }
                        }
                }

                current = list_entry->next;
        }

        book_multi_clear(values);
        values->values[0] = book_value_table_value(table);
        values->count = 1;
        return NORMAL;
}

static int book_runtime_eval_unary(
        BOOK_RUNTIME * runtime,
        BOOK_ENV * environment,
        uint32_t node_id,
        const BOOK_AST_NODE * node,
        BOOK_MULTI_VALUE * values
) {
        BOOK_MULTI_VALUE operand_values;
        BOOK_VALUE operand;

        if (!runtime || !environment || !node || !values) return ABNORMAL;

        book_multi_clear(&operand_values);
        if (
                book_runtime_eval(
                        runtime,
                        environment,
                        node->a,
                        &operand_values
                ) != NORMAL
        ) {
                return ABNORMAL;
        }

        operand = book_multi_first(&operand_values);
        book_multi_clear(values);

        switch (node->op) {
                case BOOK_TOKEN_NOT:
                        values->values[0] = book_value_boolean_value(
                                book_runtime_truthy(&operand) == ISTRUE
                                        ? ISFALSE
                                        : ISTRUE
                        );
                        values->count = 1;
                        return NORMAL;

                case BOOK_TOKEN_MINUS:
                        if (operand.type != BOOK_VALUE_NUMBER) {
                                return book_runtime_fail_node(
                                        runtime,
                                        node_id,
                                        "unary '-' requires a number"
                                );
                        }
                        values->values[0] = book_value_number_value(
                                -operand.as.number
                        );
                        values->count = 1;
                        return NORMAL;

                case BOOK_TOKEN_LENGTH:
                        if (
                                operand.type == BOOK_VALUE_STRING
                                && operand.as.string
                        ) {
                                values->values[0] = book_value_number_value(
                                        (double)operand.as.string->length
                                );
                                values->count = 1;
                                return NORMAL;
                        }

                        if (
                                operand.type == BOOK_VALUE_TABLE
                                && operand.as.table
                        ) {
                                values->values[0] = book_value_number_value(
                                        (double)book_runtime_table_length(
                                                operand.as.table
                                        )
                                );
                                values->count = 1;
                                return NORMAL;
                        }

                        return book_runtime_fail_node(
                                runtime,
                                node_id,
                                "length operator requires a string or table"
                        );

                case BOOK_TOKEN_TILDE: {
                        int64_t integer;

                        if (
                                operand.type != BOOK_VALUE_NUMBER
                                || book_runtime_number_to_integer(
                                        runtime,
                                        node_id,
                                        operand.as.number,
                                        &integer
                                ) != NORMAL
                        ) {
                                return ABNORMAL;
                        }

                        values->values[0] = book_value_number_value(
                                (double)(~integer)
                        );
                        values->count = 1;
                        return NORMAL;
                }

                default:
                        return book_runtime_fail_node(
                                runtime,
                                node_id,
                                "unsupported unary operator"
                        );
        }
}

static int book_runtime_shift(
        BOOK_RUNTIME * runtime,
        uint32_t node_id,
        int64_t left,
        int64_t right,
        int8_t shift_left,
        double * result
) {
        uint64_t value = (uint64_t)left;

        if (!result) return ABNORMAL;

        if (right < 0) {
                if (right == INT64_MIN) {
                        *result = 0;
                        return NORMAL;
                }

                return book_runtime_shift(
                        runtime,
                        node_id,
                        left,
                        -right,
                        shift_left == ISTRUE ? ISFALSE : ISTRUE,
                        result
                );
        }

        if (right >= 64) {
                *result = 0;
                return NORMAL;
        }

        if (shift_left == ISTRUE) {
                value <<= (unsigned int)right;
        } else {
                value >>= (unsigned int)right;
        }

        *result = (double)(int64_t)value;
        return NORMAL;
}

static int book_runtime_eval_binary(
        BOOK_RUNTIME * runtime,
        BOOK_ENV * environment,
        uint32_t node_id,
        const BOOK_AST_NODE * node,
        BOOK_MULTI_VALUE * values
) {
        BOOK_MULTI_VALUE left_values;
        BOOK_MULTI_VALUE right_values;
        BOOK_VALUE left;
        BOOK_VALUE right;

        if (!runtime || !environment || !node || !values) return ABNORMAL;

        book_multi_clear(&left_values);
        if (
                book_runtime_eval(
                        runtime,
                        environment,
                        node->a,
                        &left_values
                ) != NORMAL
        ) {
                return ABNORMAL;
        }

        left = book_multi_first(&left_values);

        if (node->op == BOOK_TOKEN_AND) {
                if (book_runtime_truthy(&left) != ISTRUE) {
                        book_multi_clear(values);
                        values->values[0] = left;
                        values->count = 1;
                        return NORMAL;
                }

                book_multi_clear(&right_values);
                if (
                        book_runtime_eval(
                                runtime,
                                environment,
                                node->b,
                                &right_values
                        ) != NORMAL
                ) {
                        return ABNORMAL;
                }

                book_multi_clear(values);
                values->values[0] = book_multi_first(&right_values);
                values->count = 1;
                return NORMAL;
        }

        if (node->op == BOOK_TOKEN_OR) {
                if (book_runtime_truthy(&left) == ISTRUE) {
                        book_multi_clear(values);
                        values->values[0] = left;
                        values->count = 1;
                        return NORMAL;
                }

                book_multi_clear(&right_values);
                if (
                        book_runtime_eval(
                                runtime,
                                environment,
                                node->b,
                                &right_values
                        ) != NORMAL
                ) {
                        return ABNORMAL;
                }

                book_multi_clear(values);
                values->values[0] = book_multi_first(&right_values);
                values->count = 1;
                return NORMAL;
        }

        book_multi_clear(&right_values);
        if (
                book_runtime_eval(
                        runtime,
                        environment,
                        node->b,
                        &right_values
                ) != NORMAL
        ) {
                return ABNORMAL;
        }

        right = book_multi_first(&right_values);
        book_multi_clear(values);

        if (
                node->op == BOOK_TOKEN_EQUAL_EQUAL
                || node->op == BOOK_TOKEN_NOT_EQUAL
        ) {
                int equal = book_runtime_value_equal(
                        &left,
                        &right
                );

                if (node->op == BOOK_TOKEN_NOT_EQUAL) {
                        equal = equal == ISTRUE ? ISFALSE : ISTRUE;
                }

                values->values[0] = book_value_boolean_value(equal);
                values->count = 1;
                return NORMAL;
        }

        if (
                node->op == BOOK_TOKEN_LESS
                || node->op == BOOK_TOKEN_LESS_EQUAL
                || node->op == BOOK_TOKEN_GREATER
                || node->op == BOOK_TOKEN_GREATER_EQUAL
        ) {
                int comparison = 0;

                if (
                        left.type == BOOK_VALUE_NUMBER
                        && right.type == BOOK_VALUE_NUMBER
                ) {
                        if (left.as.number < right.as.number) comparison = -1;
                        else if (left.as.number > right.as.number) comparison = 1;
                } else if (
                        left.type == BOOK_VALUE_STRING
                        && right.type == BOOK_VALUE_STRING
                        && left.as.string
                        && right.as.string
                ) {
                        c_size_t minimum = left.as.string->length
                                < right.as.string->length
                                ? left.as.string->length
                                : right.as.string->length;

                        comparison = minimum > 0
                                ? memcmp(
                                        left.as.string->bytes,
                                        right.as.string->bytes,
                                        minimum
                                )
                                : 0;

                        if (comparison == 0) {
                                if (
                                        left.as.string->length
                                        < right.as.string->length
                                ) comparison = -1;
                                else if (
                                        left.as.string->length
                                        > right.as.string->length
                                ) comparison = 1;
                        }
                } else {
                        return book_runtime_fail_node(
                                runtime,
                                node_id,
                                "Lua ordering requires two numbers or two strings"
                        );
                }

                values->values[0] = book_value_boolean_value(
                        node->op == BOOK_TOKEN_LESS
                                ? (comparison < 0 ? ISTRUE : ISFALSE)
                        : node->op == BOOK_TOKEN_LESS_EQUAL
                                ? (comparison <= 0 ? ISTRUE : ISFALSE)
                        : node->op == BOOK_TOKEN_GREATER
                                ? (comparison > 0 ? ISTRUE : ISFALSE)
                        : (comparison >= 0 ? ISTRUE : ISFALSE)
                );
                values->count = 1;
                return NORMAL;
        }

        if (node->op == BOOK_TOKEN_CONCAT) {
                const unsigned char * left_bytes;
                const unsigned char * right_bytes;
                c_size_t left_length;
                c_size_t right_length;
                char left_number[64];
                char right_number[64];
                BOOK_STRING * string;

                if (
                        book_runtime_value_to_concat(
                                runtime,
                                node_id,
                                left,
                                &left_bytes,
                                &left_length,
                                left_number
                        ) != NORMAL
                        || book_runtime_value_to_concat(
                                runtime,
                                node_id,
                                right,
                                &right_bytes,
                                &right_length,
                                right_number
                        ) != NORMAL
                ) {
                        return ABNORMAL;
                }

                if (left_length > SIZE_MAX - right_length) {
                        return book_runtime_fail_node(
                                runtime,
                                node_id,
                                "Lua concatenation is too large"
                        );
                }

                string = book_runtime_alloc(
                        runtime,
                        sizeof(*string) + left_length + right_length + 1U
                );
                if (!string) return ABNORMAL;

                string->length = left_length + right_length;
                if (left_length) {
                        memcpy(
                                string->bytes,
                                left_bytes,
                                left_length
                        );
                }
                if (right_length) {
                        memcpy(
                                string->bytes + left_length,
                                right_bytes,
                                right_length
                        );
                }
                string->bytes[string->length] = 0x00;

                values->values[0] = book_value_string_value(string);
                values->count = 1;
                return NORMAL;
        }

        if (
                left.type != BOOK_VALUE_NUMBER
                || right.type != BOOK_VALUE_NUMBER
        ) {
                return book_runtime_fail_node(
                        runtime,
                        node_id,
                        "Lua arithmetic requires numbers"
                );
        }

        switch (node->op) {
                case BOOK_TOKEN_PLUS:
                        values->values[0] = book_value_number_value(
                                left.as.number + right.as.number
                        );
                        break;

                case BOOK_TOKEN_MINUS:
                        values->values[0] = book_value_number_value(
                                left.as.number - right.as.number
                        );
                        break;

                case BOOK_TOKEN_STAR:
                        values->values[0] = book_value_number_value(
                                left.as.number * right.as.number
                        );
                        break;

                case BOOK_TOKEN_SLASH:
                        if (right.as.number == 0.0) {
                                return book_runtime_fail_node(
                                        runtime,
                                        node_id,
                                        "division by zero"
                                );
                        }
                        values->values[0] = book_value_number_value(
                                left.as.number / right.as.number
                        );
                        break;

                case BOOK_TOKEN_FLOOR_DIV:
                case BOOK_TOKEN_PERCENT: {
                        double quotient;
                        int64_t truncated;
                        double floored;

                        if (right.as.number == 0.0) {
                                return book_runtime_fail_node(
                                        runtime,
                                        node_id,
                                        "division by zero"
                                );
                        }

                        quotient = left.as.number / right.as.number;
                        if (
                                quotient < (double)INT64_MIN
                                || quotient > (double)INT64_MAX
                        ) {
                                return book_runtime_fail_node(
                                        runtime,
                                        node_id,
                                        "floor division result is outside runtime bounds"
                                );
                        }

                        truncated = (int64_t)quotient;
                        floored = (double)truncated;
                        if (floored > quotient) floored -= 1.0;

                        values->values[0] = book_value_number_value(
                                node->op == BOOK_TOKEN_FLOOR_DIV
                                        ? floored
                                        : left.as.number
                                                - (floored * right.as.number)
                        );
                        break;
                }

                case BOOK_TOKEN_BIT_AND:
                case BOOK_TOKEN_BIT_OR:
                case BOOK_TOKEN_TILDE:
                case BOOK_TOKEN_SHIFT_LEFT:
                case BOOK_TOKEN_SHIFT_RIGHT: {
                        int64_t left_integer;
                        int64_t right_integer;
                        double shifted;

                        if (
                                book_runtime_number_to_integer(
                                        runtime,
                                        node_id,
                                        left.as.number,
                                        &left_integer
                                ) != NORMAL
                                || book_runtime_number_to_integer(
                                        runtime,
                                        node_id,
                                        right.as.number,
                                        &right_integer
                                ) != NORMAL
                        ) {
                                return ABNORMAL;
                        }

                        if (node->op == BOOK_TOKEN_BIT_AND) {
                                values->values[0] = book_value_number_value(
                                        (double)(left_integer & right_integer)
                                );
                        } else if (node->op == BOOK_TOKEN_BIT_OR) {
                                values->values[0] = book_value_number_value(
                                        (double)(left_integer | right_integer)
                                );
                        } else if (node->op == BOOK_TOKEN_TILDE) {
                                values->values[0] = book_value_number_value(
                                        (double)(left_integer ^ right_integer)
                                );
                        } else {
                                if (
                                        book_runtime_shift(
                                                runtime,
                                                node_id,
                                                left_integer,
                                                right_integer,
                                                node->op == BOOK_TOKEN_SHIFT_LEFT
                                                        ? ISTRUE
                                                        : ISFALSE,
                                                &shifted
                                        ) != NORMAL
                                ) {
                                        return ABNORMAL;
                                }

                                values->values[0] = book_value_number_value(
                                        shifted
                                );
                        }
                        break;
                }

                default:
                        return book_runtime_fail_node(
                                runtime,
                                node_id,
                                "unsupported binary operator"
                        );
        }

        values->count = 1;
        return NORMAL;
}

static int book_runtime_eval(
        BOOK_RUNTIME * runtime,
        BOOK_ENV * environment,
        uint32_t node_id,
        BOOK_MULTI_VALUE * values
) {
        const BOOK_AST_NODE * node;

        if (!runtime || !environment || !values) return ABNORMAL;
        book_multi_clear(values);

        if (book_runtime_tick(runtime, node_id) != NORMAL) {
                return ABNORMAL;
        }

        node = book_program_node(runtime->program, node_id);
        if (!node) {
                return book_runtime_fail_node(
                        runtime,
                        node_id,
                        "invalid AST expression"
                );
        }

        switch (node->kind) {
                case BOOK_AST_NIL:
                        values->values[0] = book_value_nil_value();
                        values->count = 1;
                        return NORMAL;

                case BOOK_AST_BOOLEAN:
                        values->values[0] = book_value_boolean_value(
                                node->flags ? ISTRUE : ISFALSE
                        );
                        values->count = 1;
                        return NORMAL;

                case BOOK_AST_NUMBER: {
                        double number;

                        if (
                                book_runtime_number_literal(
                                        runtime,
                                        node_id,
                                        node->token,
                                        &number
                                ) != NORMAL
                        ) {
                                return ABNORMAL;
                        }

                        values->values[0] = book_value_number_value(number);
                        values->count = 1;
                        return NORMAL;
                }

                case BOOK_AST_STRING: {
                        BOOK_STRING * string = book_runtime_decoded_string(
                                runtime,
                                node->token
                        );

                        if (!string) return ABNORMAL;
                        values->values[0] = book_value_string_value(string);
                        values->count = 1;
                        return NORMAL;
                }

                case BOOK_AST_VARIABLE:
                        if (
                                book_runtime_env_get_token(
                                        runtime,
                                        environment,
                                        node->token,
                                        &values->values[0]
                                ) != NORMAL
                        ) {
                                return ABNORMAL;
                        }
                        values->count = 1;
                        return NORMAL;

                case BOOK_AST_VARARG: {
                        BOOK_ENV * current = environment;

                        while (current && current->function_scope != ISTRUE) {
                                current = current->parent;
                        }

                        if (!current) {
                                return book_runtime_fail_node(
                                        runtime,
                                        node_id,
                                        "varargs are not available in this scope"
                                );
                        }

                        for (
                                c_size_t i = 0;
                                i < current->vararg_count;
                                i++
                        ) {
                                if (
                                        book_multi_append(
                                                values,
                                                current->varargs[i]
                                        ) != NORMAL
                                ) {
                                        return book_runtime_fail_node(
                                                runtime,
                                                node_id,
                                                "Lua multiple-value limit exceeded"
                                        );
                                }
                        }

                        return NORMAL;
                }

                case BOOK_AST_TABLE:
                        return book_runtime_eval_table(
                                runtime,
                                environment,
                                node_id,
                                node,
                                values
                        );

                case BOOK_AST_FUNCTION: {
                        BOOK_FUNCTION * function = book_runtime_alloc(
                                runtime,
                                sizeof(*function)
                        );

                        if (!function) return ABNORMAL;
                        function->program = runtime->program;
                        function->node_id = node_id;
                        function->closure = environment;

                        values->values[0] = book_value_function_value(function);
                        values->count = 1;
                        return NORMAL;
                }

                case BOOK_AST_UNARY:
                        return book_runtime_eval_unary(
                                runtime,
                                environment,
                                node_id,
                                node,
                                values
                        );

                case BOOK_AST_BINARY:
                        return book_runtime_eval_binary(
                                runtime,
                                environment,
                                node_id,
                                node,
                                values
                        );

                case BOOK_AST_FIELD: {
                        BOOK_MULTI_VALUE base_values;
                        BOOK_VALUE base;
                        const unsigned char * bytes;
                        c_size_t length;

                        book_multi_clear(&base_values);
                        if (
                                book_runtime_eval(
                                        runtime,
                                        environment,
                                        node->a,
                                        &base_values
                                ) != NORMAL
                        ) {
                                return ABNORMAL;
                        }

                        base = book_multi_first(&base_values);
                        if (
                                base.type != BOOK_VALUE_TABLE
                                || !base.as.table
                        ) {
                                return book_runtime_fail_node(
                                        runtime,
                                        node_id,
                                        "field access requires a table"
                                );
                        }

                        if (
                                book_runtime_token_bytes(
                                        runtime,
                                        node->token,
                                        &bytes,
                                        &length
                                ) != NORMAL
                                || book_runtime_table_get_string(
                                        base.as.table,
                                        bytes,
                                        length,
                                        &values->values[0]
                                ) != NORMAL
                        ) {
                                return ABNORMAL;
                        }

                        values->count = 1;
                        return NORMAL;
                }

                case BOOK_AST_INDEX: {
                        BOOK_MULTI_VALUE base_values;
                        BOOK_MULTI_VALUE key_values;
                        BOOK_VALUE base;

                        book_multi_clear(&base_values);
                        book_multi_clear(&key_values);

                        if (
                                book_runtime_eval(
                                        runtime,
                                        environment,
                                        node->a,
                                        &base_values
                                ) != NORMAL
                                || book_runtime_eval(
                                        runtime,
                                        environment,
                                        node->b,
                                        &key_values
                                ) != NORMAL
                        ) {
                                return ABNORMAL;
                        }

                        base = book_multi_first(&base_values);
                        if (
                                base.type != BOOK_VALUE_TABLE
                                || !base.as.table
                        ) {
                                return book_runtime_fail_node(
                                        runtime,
                                        node_id,
                                        "index access requires a table"
                                );
                        }

                        {
                                BOOK_VALUE key = book_multi_first(&key_values);

                                if (
                                        book_runtime_table_get(
                                                runtime,
                                                node_id,
                                                base.as.table,
                                                &key,
                                                &values->values[0]
                                        ) != NORMAL
                                ) {
                                        return ABNORMAL;
                                }
                        }

                        values->count = 1;
                        return NORMAL;
                }

                case BOOK_AST_CALL: {
                        BOOK_MULTI_VALUE callable_values;
                        BOOK_MULTI_VALUE arguments;

                        book_multi_clear(&callable_values);
                        book_multi_clear(&arguments);

                        if (
                                book_runtime_eval(
                                        runtime,
                                        environment,
                                        node->a,
                                        &callable_values
                                ) != NORMAL
                                || book_runtime_eval_list(
                                        runtime,
                                        environment,
                                        node->list1,
                                        &arguments
                                ) != NORMAL
                        ) {
                                return ABNORMAL;
                        }

                        return book_runtime_call_value(
                                runtime,
                                node_id,
                                book_multi_first(&callable_values),
                                arguments.values,
                                arguments.count,
                                values
                        );
                }

                default:
                        return book_runtime_fail_node(
                                runtime,
                                node_id,
                                "AST node is not an expression"
                        );
        }
}

static int book_runtime_assign_lvalue(
        BOOK_RUNTIME * runtime,
        BOOK_ENV * environment,
        uint32_t node_id,
        BOOK_VALUE value
) {
        const BOOK_AST_NODE * node;

        if (!runtime || !environment) return ABNORMAL;
        node = book_program_node(runtime->program, node_id);
        if (!node) return ABNORMAL;

        if (node->kind == BOOK_AST_VARIABLE) {
                return book_runtime_env_assign_token(
                        runtime,
                        environment,
                        node->token,
                        value
                );
        }

        if (node->kind == BOOK_AST_FIELD) {
                BOOK_MULTI_VALUE base_values;
                BOOK_VALUE base;
                BOOK_STRING * key;

                book_multi_clear(&base_values);
                if (
                        book_runtime_eval(
                                runtime,
                                environment,
                                node->a,
                                &base_values
                        ) != NORMAL
                ) {
                        return ABNORMAL;
                }

                base = book_multi_first(&base_values);
                if (
                        base.type != BOOK_VALUE_TABLE
                        || !base.as.table
                ) {
                        return book_runtime_fail_node(
                                runtime,
                                node_id,
                                "field assignment requires a table"
                        );
                }

                key = book_runtime_string_from_token(
                        runtime,
                        node->token
                );
                if (!key) return ABNORMAL;

                return book_runtime_table_set(
                        runtime,
                        node_id,
                        base.as.table,
                        book_value_string_value(key),
                        value
                );
        }

        if (node->kind == BOOK_AST_INDEX) {
                BOOK_MULTI_VALUE base_values;
                BOOK_MULTI_VALUE key_values;
                BOOK_VALUE base;

                book_multi_clear(&base_values);
                book_multi_clear(&key_values);

                if (
                        book_runtime_eval(
                                runtime,
                                environment,
                                node->a,
                                &base_values
                        ) != NORMAL
                        || book_runtime_eval(
                                runtime,
                                environment,
                                node->b,
                                &key_values
                        ) != NORMAL
                ) {
                        return ABNORMAL;
                }

                base = book_multi_first(&base_values);
                if (
                        base.type != BOOK_VALUE_TABLE
                        || !base.as.table
                ) {
                        return book_runtime_fail_node(
                                runtime,
                                node_id,
                                "index assignment requires a table"
                        );
                }

                return book_runtime_table_set(
                        runtime,
                        node_id,
                        base.as.table,
                        book_multi_first(&key_values),
                        value
                );
        }

        return book_runtime_fail_node(
                runtime,
                node_id,
                "invalid assignment destination"
        );
}

static int book_runtime_bind_list(
        BOOK_RUNTIME * runtime,
        BOOK_ENV * environment,
        uint32_t names,
        const BOOK_MULTI_VALUE * values,
        int8_t local
) {
        uint32_t current = names;
        c_size_t index = 0;

        if (!runtime || !environment || !values) return ABNORMAL;

        while (current) {
                const BOOK_AST_LIST_ENTRY * entry;
                const BOOK_AST_NODE * variable;
                BOOK_VALUE value = index < values->count
                        ? values->values[index]
                        : book_value_nil_value();

                entry = book_program_list_entry(
                        runtime->program,
                        current
                );
                if (!entry) return ABNORMAL;

                variable = book_program_node(
                        runtime->program,
                        entry->node
                );
                if (
                        !variable
                        || variable->kind != BOOK_AST_VARIABLE
                ) {
                        return book_runtime_fail_node(
                                runtime,
                                entry->node,
                                "invalid variable binding"
                        );
                }

                if (local == ISTRUE) {
                        if (
                                book_runtime_env_define_token(
                                        runtime,
                                        environment,
                                        variable->token,
                                        value
                                ) != NORMAL
                        ) {
                                return ABNORMAL;
                        }
                } else if (
                        book_runtime_assign_lvalue(
                                runtime,
                                environment,
                                entry->node,
                                value
                        ) != NORMAL
                ) {
                        return ABNORMAL;
                }

                index++;
                current = entry->next;
        }

        return NORMAL;
}

static book_flow_t book_runtime_exec_statement(
        BOOK_RUNTIME * runtime,
        BOOK_ENV * environment,
        uint32_t node_id,
        BOOK_MULTI_VALUE * returns
) {
        const BOOK_AST_NODE * node;

        if (!runtime || !environment || !returns) return BOOK_FLOW_ERROR;
        if (book_runtime_tick(runtime, node_id) != NORMAL) return BOOK_FLOW_ERROR;

        node = book_program_node(runtime->program, node_id);
        if (!node) {
                (void)book_runtime_fail_node(
                        runtime,
                        node_id,
                        "invalid AST statement"
                );
                return BOOK_FLOW_ERROR;
        }

        switch (node->kind) {
                case BOOK_AST_EXPR_STATEMENT: {
                        BOOK_MULTI_VALUE ignored;

                        book_multi_clear(&ignored);
                        if (
                                book_runtime_eval(
                                        runtime,
                                        environment,
                                        node->a,
                                        &ignored
                                ) != NORMAL
                        ) {
                                return BOOK_FLOW_ERROR;
                        }

                        return BOOK_FLOW_NORMAL;
                }

                case BOOK_AST_LOCAL_ASSIGN: {
                        BOOK_MULTI_VALUE values;

                        book_multi_clear(&values);
                        if (
                                node->list2
                                && book_runtime_eval_list(
                                        runtime,
                                        environment,
                                        node->list2,
                                        &values
                                ) != NORMAL
                        ) {
                                return BOOK_FLOW_ERROR;
                        }

                        if (
                                book_runtime_bind_list(
                                        runtime,
                                        environment,
                                        node->list1,
                                        &values,
                                        ISTRUE
                                ) != NORMAL
                        ) {
                                return BOOK_FLOW_ERROR;
                        }

                        return BOOK_FLOW_NORMAL;
                }

                case BOOK_AST_ASSIGN: {
                        BOOK_MULTI_VALUE values;
                        uint32_t current = node->list1;
                        c_size_t index = 0;

                        book_multi_clear(&values);
                        if (
                                book_runtime_eval_list(
                                        runtime,
                                        environment,
                                        node->list2,
                                        &values
                                ) != NORMAL
                        ) {
                                return BOOK_FLOW_ERROR;
                        }

                        while (current) {
                                const BOOK_AST_LIST_ENTRY * entry =
                                        book_program_list_entry(
                                                runtime->program,
                                                current
                                        );
                                BOOK_VALUE value;

                                if (!entry) return BOOK_FLOW_ERROR;

                                value = index < values.count
                                        ? values.values[index]
                                        : book_value_nil_value();

                                if (
                                        book_runtime_assign_lvalue(
                                                runtime,
                                                environment,
                                                entry->node,
                                                value
                                        ) != NORMAL
                                ) {
                                        return BOOK_FLOW_ERROR;
                                }

                                index++;
                                current = entry->next;
                        }

                        return BOOK_FLOW_NORMAL;
                }

                case BOOK_AST_LOCAL_FUNCTION: {
                        BOOK_VALUE nil = book_value_nil_value();
                        BOOK_MULTI_VALUE function_values;
                        const BOOK_AST_NODE * function_node;

                        if (
                                book_runtime_env_define_token(
                                        runtime,
                                        environment,
                                        node->token,
                                        nil
                                ) != NORMAL
                        ) {
                                return BOOK_FLOW_ERROR;
                        }

                        function_node = book_program_node(
                                runtime->program,
                                node->a
                        );
                        if (!function_node) return BOOK_FLOW_ERROR;

                        book_multi_clear(&function_values);
                        if (
                                book_runtime_eval(
                                        runtime,
                                        environment,
                                        node->a,
                                        &function_values
                                ) != NORMAL
                        ) {
                                return BOOK_FLOW_ERROR;
                        }

                        {
                                const unsigned char * bytes;
                                c_size_t length;
                                BOOK_BINDING * binding;

                                if (
                                        book_runtime_token_bytes(
                                                runtime,
                                                node->token,
                                                &bytes,
                                                &length
                                        ) != NORMAL
                                ) {
                                        return BOOK_FLOW_ERROR;
                                }

                                binding = book_runtime_env_find_local_bytes(
                                        environment,
                                        bytes,
                                        length
                                );
                                if (!binding) return BOOK_FLOW_ERROR;
                                binding->value = book_multi_first(
                                        &function_values
                                );
                        }

                        return BOOK_FLOW_NORMAL;
                }

                case BOOK_AST_FUNCTION_DECL: {
                        BOOK_MULTI_VALUE function_values;

                        book_multi_clear(&function_values);
                        if (
                                book_runtime_eval(
                                        runtime,
                                        environment,
                                        node->b,
                                        &function_values
                                ) != NORMAL
                                || book_runtime_assign_lvalue(
                                        runtime,
                                        environment,
                                        node->a,
                                        book_multi_first(&function_values)
                                ) != NORMAL
                        ) {
                                return BOOK_FLOW_ERROR;
                        }

                        return BOOK_FLOW_NORMAL;
                }

                case BOOK_AST_IF: {
                        uint32_t current = node->list1;

                        while (current) {
                                const BOOK_AST_LIST_ENTRY * entry =
                                        book_program_list_entry(
                                                runtime->program,
                                                current
                                        );
                                const BOOK_AST_NODE * clause;
                                int8_t matches = ISTRUE;

                                if (!entry) return BOOK_FLOW_ERROR;
                                clause = book_program_node(
                                        runtime->program,
                                        entry->node
                                );
                                if (
                                        !clause
                                        || clause->kind != BOOK_AST_IF_CLAUSE
                                ) {
                                        return BOOK_FLOW_ERROR;
                                }

                                if (clause->a) {
                                        BOOK_MULTI_VALUE condition;

                                        book_multi_clear(&condition);
                                        if (
                                                book_runtime_eval(
                                                        runtime,
                                                        environment,
                                                        clause->a,
                                                        &condition
                                                ) != NORMAL
                                        ) {
                                                return BOOK_FLOW_ERROR;
                                        }

                                        {
                                                BOOK_VALUE condition_value =
                                                        book_multi_first(&condition);

                                                matches = book_runtime_truthy(
                                                        &condition_value
                                                );
                                        }
                                }

                                if (matches == ISTRUE) {
                                        BOOK_ENV * scope =
                                                book_runtime_env_new(
                                                        runtime,
                                                        environment,
                                                        ISFALSE
                                                );
                                        book_flow_t flow;

                                        if (!scope) return BOOK_FLOW_ERROR;

                                        flow = book_runtime_exec_block(
                                                runtime,
                                                scope,
                                                clause->b,
                                                returns
                                        );
                                        return flow;
                                }

                                current = entry->next;
                        }

                        return BOOK_FLOW_NORMAL;
                }

                case BOOK_AST_WHILE: {
                        for (;;) {
                                BOOK_MULTI_VALUE condition;
                                BOOK_ENV * scope;
                                book_flow_t flow;

                                book_multi_clear(&condition);
                                if (
                                        book_runtime_eval(
                                                runtime,
                                                environment,
                                                node->a,
                                                &condition
                                        ) != NORMAL
                                ) {
                                        return BOOK_FLOW_ERROR;
                                }

                                {
                                        BOOK_VALUE condition_value =
                                                book_multi_first(&condition);

                                        if (
                                                book_runtime_truthy(
                                                        &condition_value
                                                ) != ISTRUE
                                        ) {
                                                return BOOK_FLOW_NORMAL;
                                        }
                                }

                                scope = book_runtime_env_new(
                                        runtime,
                                        environment,
                                        ISFALSE
                                );
                                if (!scope) return BOOK_FLOW_ERROR;

                                flow = book_runtime_exec_block(
                                        runtime,
                                        scope,
                                        node->b,
                                        returns
                                );

                                if (flow == BOOK_FLOW_BREAK) {
                                        return BOOK_FLOW_NORMAL;
                                }
                                if (flow != BOOK_FLOW_NORMAL) return flow;
                        }
                }

                case BOOK_AST_FOR_NUMERIC: {
                        BOOK_MULTI_VALUE first_values;
                        BOOK_MULTI_VALUE last_values;
                        BOOK_MULTI_VALUE step_values;
                        BOOK_VALUE first;
                        BOOK_VALUE last;
                        BOOK_VALUE step = book_value_number_value(1.0);
                        BOOK_ENV * loop_scope;
                        double current;

                        book_multi_clear(&first_values);
                        book_multi_clear(&last_values);
                        book_multi_clear(&step_values);

                        if (
                                book_runtime_eval(
                                        runtime,
                                        environment,
                                        node->a,
                                        &first_values
                                ) != NORMAL
                                || book_runtime_eval(
                                        runtime,
                                        environment,
                                        node->b,
                                        &last_values
                                ) != NORMAL
                        ) {
                                return BOOK_FLOW_ERROR;
                        }

                        if (node->c) {
                                if (
                                        book_runtime_eval(
                                                runtime,
                                                environment,
                                                node->c,
                                                &step_values
                                        ) != NORMAL
                                ) {
                                        return BOOK_FLOW_ERROR;
                                }
                                step = book_multi_first(&step_values);
                        }

                        first = book_multi_first(&first_values);
                        last = book_multi_first(&last_values);

                        if (
                                first.type != BOOK_VALUE_NUMBER
                                || last.type != BOOK_VALUE_NUMBER
                                || step.type != BOOK_VALUE_NUMBER
                                || step.as.number == 0.0
                        ) {
                                (void)book_runtime_fail_node(
                                        runtime,
                                        node_id,
                                        "numeric for loop requires numeric nonzero bounds/step"
                                );
                                return BOOK_FLOW_ERROR;
                        }

                        loop_scope = book_runtime_env_new(
                                runtime,
                                environment,
                                ISFALSE
                        );
                        if (!loop_scope) return BOOK_FLOW_ERROR;

                        if (
                                book_runtime_env_define_token(
                                        runtime,
                                        loop_scope,
                                        node->token,
                                        first
                                ) != NORMAL
                        ) {
                                return BOOK_FLOW_ERROR;
                        }

                        current = first.as.number;

                        while (
                                step.as.number > 0.0
                                        ? current <= last.as.number
                                        : current >= last.as.number
                        ) {
                                const unsigned char * bytes;
                                c_size_t length;
                                BOOK_BINDING * binding;
                                BOOK_ENV * iteration_scope;
                                book_flow_t flow;

                                if (book_runtime_tick(runtime, node_id) != NORMAL) {
                                        return BOOK_FLOW_ERROR;
                                }

                                if (
                                        book_runtime_token_bytes(
                                                runtime,
                                                node->token,
                                                &bytes,
                                                &length
                                        ) != NORMAL
                                ) {
                                        return BOOK_FLOW_ERROR;
                                }

                                binding = book_runtime_env_find_local_bytes(
                                        loop_scope,
                                        bytes,
                                        length
                                );
                                if (!binding) return BOOK_FLOW_ERROR;
                                binding->value = book_value_number_value(current);

                                iteration_scope = book_runtime_env_new(
                                        runtime,
                                        loop_scope,
                                        ISFALSE
                                );
                                if (!iteration_scope) return BOOK_FLOW_ERROR;

                                flow = book_runtime_exec_block(
                                        runtime,
                                        iteration_scope,
                                        node->d,
                                        returns
                                );

                                if (flow == BOOK_FLOW_BREAK) {
                                        return BOOK_FLOW_NORMAL;
                                }
                                if (flow != BOOK_FLOW_NORMAL) return flow;

                                current += step.as.number;
                        }

                        return BOOK_FLOW_NORMAL;
                }

                case BOOK_AST_FOR_GENERIC: {
                        BOOK_MULTI_VALUE iterator_values;
                        BOOK_VALUE iterator;
                        BOOK_VALUE state_value;
                        BOOK_VALUE control;
                        BOOK_ENV * loop_scope;

                        book_multi_clear(&iterator_values);
                        if (
                                book_runtime_eval_list(
                                        runtime,
                                        environment,
                                        node->list2,
                                        &iterator_values
                                ) != NORMAL
                        ) {
                                return BOOK_FLOW_ERROR;
                        }

                        iterator = iterator_values.count > 0
                                ? iterator_values.values[0]
                                : book_value_nil_value();
                        state_value = iterator_values.count > 1
                                ? iterator_values.values[1]
                                : book_value_nil_value();
                        control = iterator_values.count > 2
                                ? iterator_values.values[2]
                                : book_value_nil_value();

                        loop_scope = book_runtime_env_new(
                                runtime,
                                environment,
                                ISFALSE
                        );
                        if (!loop_scope) return BOOK_FLOW_ERROR;

                        for (;;) {
                                BOOK_VALUE call_arguments[2];
                                BOOK_MULTI_VALUE generated;
                                BOOK_ENV * iteration_scope;
                                uint32_t current_name;
                                c_size_t value_index = 0;
                                book_flow_t flow;

                                if (book_runtime_tick(runtime, node_id) != NORMAL) {
                                        return BOOK_FLOW_ERROR;
                                }

                                call_arguments[0] = state_value;
                                call_arguments[1] = control;
                                book_multi_clear(&generated);

                                if (
                                        book_runtime_call_value(
                                                runtime,
                                                node_id,
                                                iterator,
                                                call_arguments,
                                                2,
                                                &generated
                                        ) != NORMAL
                                ) {
                                        return BOOK_FLOW_ERROR;
                                }

                                if (
                                        generated.count == 0
                                        || generated.values[0].type
                                                == BOOK_VALUE_NIL
                                ) {
                                        return BOOK_FLOW_NORMAL;
                                }

                                control = generated.values[0];
                                iteration_scope = book_runtime_env_new(
                                        runtime,
                                        loop_scope,
                                        ISFALSE
                                );
                                if (!iteration_scope) return BOOK_FLOW_ERROR;

                                current_name = node->list1;
                                while (current_name) {
                                        const BOOK_AST_LIST_ENTRY * entry =
                                                book_program_list_entry(
                                                        runtime->program,
                                                        current_name
                                                );
                                        const BOOK_AST_NODE * variable;
                                        BOOK_VALUE value;

                                        if (!entry) return BOOK_FLOW_ERROR;
                                        variable = book_program_node(
                                                runtime->program,
                                                entry->node
                                        );
                                        if (!variable) return BOOK_FLOW_ERROR;

                                        value = value_index < generated.count
                                                ? generated.values[value_index]
                                                : book_value_nil_value();

                                        if (
                                                book_runtime_env_define_token(
                                                        runtime,
                                                        iteration_scope,
                                                        variable->token,
                                                        value
                                                ) != NORMAL
                                        ) {
                                                return BOOK_FLOW_ERROR;
                                        }

                                        value_index++;
                                        current_name = entry->next;
                                }

                                flow = book_runtime_exec_block(
                                        runtime,
                                        iteration_scope,
                                        node->a,
                                        returns
                                );

                                if (flow == BOOK_FLOW_BREAK) {
                                        return BOOK_FLOW_NORMAL;
                                }
                                if (flow != BOOK_FLOW_NORMAL) return flow;
                        }
                }

                case BOOK_AST_BREAK:
                        return BOOK_FLOW_BREAK;

                case BOOK_AST_RETURN:
                        book_multi_clear(returns);
                        if (
                                node->list1
                                && book_runtime_eval_list(
                                        runtime,
                                        environment,
                                        node->list1,
                                        returns
                                ) != NORMAL
                        ) {
                                return BOOK_FLOW_ERROR;
                        }
                        return BOOK_FLOW_RETURN;

                default:
                        (void)book_runtime_fail_node(
                                runtime,
                                node_id,
                                "AST node is not an executable statement"
                        );
                        return BOOK_FLOW_ERROR;
        }
}

static book_flow_t book_runtime_exec_block(
        BOOK_RUNTIME * runtime,
        BOOK_ENV * environment,
        uint32_t first_statement,
        BOOK_MULTI_VALUE * returns
) {
        uint32_t current = first_statement;

        if (!runtime || !environment || !returns) return BOOK_FLOW_ERROR;

        while (current) {
                const BOOK_AST_NODE * statement =
                        book_program_node(
                                runtime->program,
                                current
                        );
                book_flow_t flow;

                if (!statement) {
                        (void)book_runtime_fail_node(
                                runtime,
                                current,
                                "invalid statement chain"
                        );
                        return BOOK_FLOW_ERROR;
                }

                flow = book_runtime_exec_statement(
                        runtime,
                        environment,
                        current,
                        returns
                );
                if (flow != BOOK_FLOW_NORMAL) return flow;

                current = statement->next;
        }

        return BOOK_FLOW_NORMAL;
}

static int book_runtime_bind_function_arguments(
        BOOK_RUNTIME * runtime,
        BOOK_ENV * environment,
        const BOOK_AST_NODE * function,
        const BOOK_VALUE * arguments,
        c_size_t argument_count
) {
        uint32_t current;
        c_size_t argument_index = 0;

        if (!runtime || !environment || !function) return ABNORMAL;

        current = function->list1;
        while (current) {
                const BOOK_AST_LIST_ENTRY * entry =
                        book_program_list_entry(
                                runtime->program,
                                current
                        );
                const BOOK_AST_NODE * parameter;
                BOOK_VALUE value;

                if (!entry) return ABNORMAL;
                parameter = book_program_node(
                        runtime->program,
                        entry->node
                );
                if (!parameter) return ABNORMAL;

                value = argument_index < argument_count
                        ? arguments[argument_index]
                        : book_value_nil_value();

                if (
                        book_runtime_env_define_token(
                                runtime,
                                environment,
                                parameter->token,
                                value
                        ) != NORMAL
                ) {
                        return ABNORMAL;
                }

                argument_index++;
                current = entry->next;
        }

        if (function->flags & BOOK_AST_FUNCTION_VARARG) {
                c_size_t vararg_count = argument_count > argument_index
                        ? argument_count - argument_index
                        : 0;

                if (vararg_count > BOOK_RUNTIME_MULTI_LIMIT) {
                        return book_runtime_fail_token(
                                runtime,
                                NULL,
                                "Lua vararg limit exceeded"
                        );
                }

                if (vararg_count > 0) {
                        environment->varargs = book_runtime_alloc(
                                runtime,
                                vararg_count * sizeof(*environment->varargs)
                        );
                        if (!environment->varargs) return ABNORMAL;

                        memcpy(
                                environment->varargs,
                                arguments + argument_index,
                                vararg_count * sizeof(*environment->varargs)
                        );
                }

                environment->vararg_count = vararg_count;
        }

        return NORMAL;
}

static int book_runtime_call_value(
        BOOK_RUNTIME * runtime,
        uint32_t call_node_id,
        BOOK_VALUE callable,
        const BOOK_VALUE * arguments,
        c_size_t argument_count,
        BOOK_MULTI_VALUE * returns
) {
        if (!runtime || !returns) return ABNORMAL;
        book_multi_clear(returns);

        if (runtime->call_depth >= BOOK_RUNTIME_CALL_DEPTH_LIMIT) {
                return book_runtime_fail_node(
                        runtime,
                        call_node_id,
                        "Lua call depth limit exceeded"
                );
        }

        runtime->call_depth++;

        if (
                callable.type == BOOK_VALUE_NATIVE_FUNCTION
                && callable.as.native_function
                && callable.as.native_function->callback
        ) {
                int status = callable.as.native_function->callback(
                        runtime,
                        arguments,
                        argument_count,
                        returns
                );

                runtime->call_depth--;
                return status;
        }

        if (
                callable.type == BOOK_VALUE_FUNCTION
                && callable.as.function
        ) {
                BOOK_PROGRAM * saved_program = runtime->program;
                const BOOK_AST_NODE * function;
                BOOK_ENV * environment;
                BOOK_MULTI_VALUE function_returns;
                book_flow_t flow;

                if (
                        !callable.as.function->program
                        || callable.as.function->program->status != NORMAL
                ) {
                        runtime->call_depth--;
                        return book_runtime_fail_node(
                                runtime,
                                call_node_id,
                                "invalid Lua function program"
                        );
                }

                runtime->program = callable.as.function->program;
                function = book_program_node(
                        runtime->program,
                        callable.as.function->node_id
                );

                if (!function || function->kind != BOOK_AST_FUNCTION) {
                        runtime->program = saved_program;
                        runtime->call_depth--;
                        return book_runtime_fail_node(
                                runtime,
                                call_node_id,
                                "invalid Lua function"
                        );
                }

                environment = book_runtime_env_new(
                        runtime,
                        callable.as.function->closure,
                        ISTRUE
                );
                if (!environment) {
                        runtime->program = saved_program;
                        runtime->call_depth--;
                        return ABNORMAL;
                }

                if (
                        book_runtime_bind_function_arguments(
                                runtime,
                                environment,
                                function,
                                arguments,
                                argument_count
                        ) != NORMAL
                ) {
                        runtime->program = saved_program;
                        runtime->call_depth--;
                        return ABNORMAL;
                }

                book_multi_clear(&function_returns);
                flow = book_runtime_exec_block(
                        runtime,
                        environment,
                        function->a,
                        &function_returns
                );

                runtime->program = saved_program;
                runtime->call_depth--;

                if (flow == BOOK_FLOW_ERROR) return ABNORMAL;
                if (flow == BOOK_FLOW_BREAK) {
                        return book_runtime_fail_node(
                                runtime,
                                call_node_id,
                                "break escaped function boundary"
                        );
                }

                if (flow == BOOK_FLOW_RETURN) {
                        *returns = function_returns;
                }

                return NORMAL;
        }

        runtime->call_depth--;
        return book_runtime_fail_node(
                runtime,
                call_node_id,
                "attempt to call a non-function value"
        );
}

static int book_runtime_native_book(
        BOOK_RUNTIME * runtime,
        const BOOK_VALUE * arguments,
        c_size_t argument_count,
        BOOK_MULTI_VALUE * returns
) {
        BOOK_VALUE name;

        if (!runtime || !returns) return ABNORMAL;
        book_multi_clear(returns);

        if (runtime->module_execution_depth > 0) {
                return book_runtime_fail_token(
                        runtime,
                        NULL,
                        "book() is not permitted inside Lua modules"
                );
        }

        if (
                argument_count != 1
                || arguments[0].type != BOOK_VALUE_TABLE
                || !arguments[0].as.table
        ) {
                return book_runtime_fail_token(
                        runtime,
                        NULL,
                        "book() requires one declaration table"
                );
        }

        if (runtime->declared_book == ISTRUE) {
                return book_runtime_fail_token(
                        runtime,
                        NULL,
                        "book() may only be declared once"
                );
        }

        if (
                book_runtime_table_get_string(
                        arguments[0].as.table,
                        (const unsigned char*)"name",
                        4,
                        &name
                ) != NORMAL
                || name.type != BOOK_VALUE_STRING
                || !name.as.string
        ) {
                return book_runtime_fail_token(
                        runtime,
                        NULL,
                        "book declaration requires a string name"
                );
        }

        if (
                !runtime->session
                || book_runtime_string_equals_bytes(
                        name.as.string,
                        runtime->session->book_name,
                        (c_size_t)strlen(
                                (const char*)runtime->session->book_name
                        )
                ) != ISTRUE
        ) {
                return book_runtime_fail_token(
                        runtime,
                        NULL,
                        "book declaration name does not match source identity"
                );
        }

        runtime->declared_book = ISTRUE;
        return NORMAL;
}

static int book_runtime_register_native(
        BOOK_RUNTIME * runtime,
        const char * name,
        book_native_callback_t callback
) {
        BOOK_NATIVE_FUNCTION * function;
        BOOK_STRING * string;

        if (!runtime || !name || !callback) return ABNORMAL;

        function = book_runtime_alloc(
                runtime,
                sizeof(*function)
        );
        if (!function) return ABNORMAL;

        function->name = name;
        function->callback = callback;

        string = book_runtime_string_new(
                runtime,
                (const unsigned char*)name,
                (c_size_t)strlen(name)
        );
        if (!string) return ABNORMAL;

        return book_runtime_env_define_string(
                runtime,
                runtime->global,
                string,
                book_value_native_value(function)
        );
}

BOOK_RUNTIME * book_runtime_create(
        BOOK_PROGRAM * program,
        BOOK_SESSION * session
) {
        BOOK_RUNTIME * runtime;

        if (
                !program
                || program->status != NORMAL
                || !session
        ) {
                return NULL;
        }

        runtime = calloc(1, sizeof(*runtime));
        if (!runtime) return NULL;

        runtime->program = program;
        runtime->session = session;
        runtime->status = NORMAL;
        runtime->prng_state = session->started_ns
                ? session->started_ns
                : 0x9e3779b97f4a7c15ULL;

        runtime->global = book_runtime_env_new(
                runtime,
                NULL,
                ISTRUE
        );
        if (!runtime->global) {
                book_runtime_destroy(runtime);
                return NULL;
        }

        if (
                book_runtime_register_native(
                        runtime,
                        "book",
                        book_runtime_native_book
                ) != NORMAL
                || book_stdlib_install(runtime) != NORMAL
                || book_modules_install(runtime) != NORMAL
                || book_native_install(runtime) != NORMAL
        ) {
                book_runtime_destroy(runtime);
                return NULL;
        }

        return runtime;
}

int book_runtime_execute(
        BOOK_RUNTIME * runtime,
        BOOK_MULTI_VALUE * returns
) {
        book_flow_t flow;

        if (!runtime || !returns) return ABNORMAL;
        book_multi_clear(returns);

        if (
                !runtime->program
                || runtime->program->status != NORMAL
                || !runtime->global
        ) {
                return book_runtime_fail_token(
                        runtime,
                        NULL,
                        "Lua runtime is not initialized"
                );
        }

        flow = book_runtime_exec_block(
                runtime,
                runtime->global,
                runtime->program->root,
                returns
        );

        if (flow == BOOK_FLOW_ERROR) return ABNORMAL;
        if (flow == BOOK_FLOW_BREAK) {
                return book_runtime_fail_token(
                        runtime,
                        NULL,
                        "break escaped top-level chunk"
                );
        }

        if (runtime->declared_book != ISTRUE) {
                return book_runtime_fail_token(
                        runtime,
                        NULL,
                        "Book source did not declare book { name = ... }"
                );
        }

        runtime->status = NORMAL;
        return NORMAL;
}

int book_runtime_execute_module(
        BOOK_RUNTIME * runtime,
        BOOK_PROGRAM * program,
        int8_t trusted_system_module,
        BOOK_MULTI_VALUE * returns
) {
        BOOK_PROGRAM * saved_program;
        BOOK_ENV * environment;
        book_flow_t flow;

        if (
                !runtime
                || !program
                || program->status != NORMAL
                || !returns
                || !runtime->global
        ) {
                return ABNORMAL;
        }

        book_multi_clear(returns);
        environment = book_runtime_env_new(
                runtime,
                runtime->global,
                ISTRUE
        );
        if (!environment) return ABNORMAL;

        if (trusted_system_module == ISTRUE) {
                BOOK_VALUE private_table;
                BOOK_STRING * private_name;

                memset(&private_table, 0x00, sizeof(private_table));

                if (
                        book_native_private_table(
                                runtime,
                                &private_table
                        ) != NORMAL
                ) {
                        return book_runtime_fail_token(
                                runtime,
                                NULL,
                                "private _kami ABI is unavailable"
                        );
                }

                private_name = book_runtime_string_new(
                        runtime,
                        (const unsigned char*)"_kami",
                        5
                );
                if (
                        !private_name
                        || book_runtime_env_define_string(
                                runtime,
                                environment,
                                private_name,
                                private_table
                        ) != NORMAL
                ) {
                        return ABNORMAL;
                }
        }

        saved_program = runtime->program;
        runtime->program = program;
        runtime->module_execution_depth++;

        flow = book_runtime_exec_block(
                runtime,
                environment,
                program->root,
                returns
        );

        runtime->module_execution_depth--;
        runtime->program = saved_program;

        if (flow == BOOK_FLOW_ERROR) return ABNORMAL;
        if (flow == BOOK_FLOW_BREAK) {
                return book_runtime_fail_token(
                        runtime,
                        NULL,
                        "break escaped module boundary"
                );
        }

        return NORMAL;
}

BOOK_SESSION * book_runtime_session(BOOK_RUNTIME * runtime) {
        return runtime ? runtime->session : NULL;
}

void * book_runtime_module_state(BOOK_RUNTIME * runtime) {
        return runtime ? runtime->module_state : NULL;
}

void book_runtime_set_module_state(BOOK_RUNTIME * runtime, void * state) {
        if (!runtime) return;
        runtime->module_state = state;
}

void * book_runtime_native_state(BOOK_RUNTIME * runtime) {
        return runtime ? runtime->native_state : NULL;
}

void book_runtime_set_native_state(BOOK_RUNTIME * runtime, void * state) {
        if (!runtime) return;
        runtime->native_state = state;
}

void book_runtime_destroy(BOOK_RUNTIME * runtime) {
        BOOK_RUNTIME_ALLOCATION * allocation;

        if (!runtime) return;

        book_native_destroy(runtime);
        book_modules_destroy(runtime);

        allocation = runtime->allocations;
        while (allocation) {
                BOOK_RUNTIME_ALLOCATION * next = allocation->next;
                c_size_t total = sizeof(*allocation) + allocation->size;

                memset(allocation, 0x00, total);
                free(allocation);
                allocation = next;
        }

        memset(runtime, 0x00, sizeof(*runtime));
        free(runtime);
        return;
}

const char * book_runtime_error(const BOOK_RUNTIME * runtime) {
        return runtime ? runtime->error : "";
}

unsigned int book_runtime_error_line(const BOOK_RUNTIME * runtime) {
        return runtime ? runtime->error_line : 0;
}

unsigned int book_runtime_error_column(const BOOK_RUNTIME * runtime) {
        return runtime ? runtime->error_column : 0;
}

uint64_t book_runtime_instruction_count(const BOOK_RUNTIME * runtime) {
        return runtime ? runtime->instructions : 0;
}

c_size_t book_runtime_memory_used(const BOOK_RUNTIME * runtime) {
        return runtime ? runtime->memory_used : 0;
}

int book_value_is_nil(const BOOK_VALUE * value) {
        return value && value->type == BOOK_VALUE_NIL
                ? ISTRUE
                : ISFALSE;
}

int book_value_boolean(const BOOK_VALUE * value, int8_t * result) {
        if (
                !value
                || !result
                || value->type != BOOK_VALUE_BOOLEAN
        ) {
                return ABNORMAL;
        }

        *result = value->as.boolean;
        return NORMAL;
}

int book_value_number(const BOOK_VALUE * value, double * result) {
        if (
                !value
                || !result
                || value->type != BOOK_VALUE_NUMBER
        ) {
                return ABNORMAL;
        }

        *result = value->as.number;
        return NORMAL;
}

int book_value_string(
        const BOOK_VALUE * value,
        const unsigned char ** bytes,
        c_size_t * length
) {
        if (
                !value
                || !bytes
                || !length
                || value->type != BOOK_VALUE_STRING
                || !value->as.string
        ) {
                return ABNORMAL;
        }

        *bytes = value->as.string->bytes;
        *length = value->as.string->length;
        return NORMAL;
}

int book_runtime_native_fail(
        BOOK_RUNTIME * runtime,
        const char * message
) {
        return book_runtime_fail_token(runtime, NULL, message);
}

int book_runtime_register_global(
        BOOK_RUNTIME * runtime,
        const char * name,
        book_native_callback_t callback
) {
        return book_runtime_register_native(runtime, name, callback);
}

int book_runtime_set_global(
        BOOK_RUNTIME * runtime,
        const char * name,
        BOOK_VALUE value
) {
        BOOK_STRING * string;

        if (!runtime || !name) return ABNORMAL;

        string = book_runtime_string_new(
                runtime,
                (const unsigned char*)name,
                (c_size_t)strlen(name)
        );
        if (!string) return ABNORMAL;

        return book_runtime_env_define_string(
                runtime,
                runtime->global,
                string,
                value
        );
}

int book_runtime_make_string(
        BOOK_RUNTIME * runtime,
        const unsigned char * bytes,
        c_size_t length,
        BOOK_VALUE * value
) {
        BOOK_STRING * string;

        if (!runtime || !value || (!bytes && length > 0)) return ABNORMAL;
        string = book_runtime_string_new(runtime, bytes, length);
        if (!string) return ABNORMAL;

        *value = book_value_string_value(string);
        return NORMAL;
}

int book_runtime_make_table(
        BOOK_RUNTIME * runtime,
        BOOK_VALUE * value
) {
        BOOK_TABLE * table;

        if (!runtime || !value) return ABNORMAL;
        table = book_runtime_table_new(runtime);
        if (!table) return ABNORMAL;

        *value = book_value_table_value(table);
        return NORMAL;
}

int book_runtime_make_native(
        BOOK_RUNTIME * runtime,
        const char * name,
        book_native_callback_t callback,
        BOOK_VALUE * value
) {
        BOOK_NATIVE_FUNCTION * function;

        if (!runtime || !name || !callback || !value) return ABNORMAL;

        function = book_runtime_alloc(runtime, sizeof(*function));
        if (!function) return ABNORMAL;

        function->name = name;
        function->callback = callback;
        *value = book_value_native_value(function);
        return NORMAL;
}

BOOK_VALUE book_runtime_nil(void) {
        return book_value_nil_value();
}

BOOK_VALUE book_runtime_boolean(int8_t value) {
        return book_value_boolean_value(value);
}

BOOK_VALUE book_runtime_number(double value) {
        return book_value_number_value(value);
}

int book_runtime_truthy_value(const BOOK_VALUE * value) {
        return book_runtime_truthy(value);
}

int book_runtime_call_native_value(
        BOOK_RUNTIME * runtime,
        BOOK_VALUE callable,
        const BOOK_VALUE * arguments,
        c_size_t argument_count,
        BOOK_MULTI_VALUE * returns
) {
        return book_runtime_call_value(
                runtime,
                0,
                callable,
                arguments,
                argument_count,
                returns
        );
}

int book_runtime_table_set_value(
        BOOK_RUNTIME * runtime,
        BOOK_VALUE table,
        BOOK_VALUE key,
        BOOK_VALUE value
) {
        if (table.type != BOOK_VALUE_TABLE || !table.as.table) return ABNORMAL;

        return book_runtime_table_set(
                runtime,
                0,
                table.as.table,
                key,
                value
        );
}

int book_runtime_table_get_value(
        BOOK_RUNTIME * runtime,
        BOOK_VALUE table,
        const BOOK_VALUE * key,
        BOOK_VALUE * value
) {
        if (
                table.type != BOOK_VALUE_TABLE
                || !table.as.table
                || !key
                || !value
        ) {
                return ABNORMAL;
        }

        return book_runtime_table_get(
                runtime,
                0,
                table.as.table,
                key,
                value
        );
}

int book_runtime_table_set_string(
        BOOK_RUNTIME * runtime,
        BOOK_VALUE table,
        const char * key,
        BOOK_VALUE value
) {
        BOOK_VALUE string_key;

        if (!runtime || !key) return ABNORMAL;
        if (
                book_runtime_make_string(
                        runtime,
                        (const unsigned char*)key,
                        (c_size_t)strlen(key),
                        &string_key
                ) != NORMAL
        ) {
                return ABNORMAL;
        }

        return book_runtime_table_set_value(
                runtime,
                table,
                string_key,
                value
        );
}

int book_runtime_table_get_string_value(
        BOOK_RUNTIME * runtime,
        BOOK_VALUE table,
        const char * key,
        BOOK_VALUE * value
) {
        if (
                !runtime
                || !key
                || !value
                || table.type != BOOK_VALUE_TABLE
                || !table.as.table
        ) {
                return ABNORMAL;
        }

        return book_runtime_table_get_string(
                table.as.table,
                (const unsigned char*)key,
                (c_size_t)strlen(key),
                value
        );
}

int book_runtime_table_remove_value(
        BOOK_RUNTIME * runtime,
        BOOK_VALUE table,
        const BOOK_VALUE * key,
        BOOK_VALUE * removed
) {
        BOOK_TABLE * raw;

        if (
                !runtime
                || table.type != BOOK_VALUE_TABLE
                || !table.as.table
                || !key
        ) {
                return ABNORMAL;
        }

        raw = table.as.table;
        if (book_runtime_table_key_valid(runtime, 0, key) != NORMAL) {
                return ABNORMAL;
        }

        for (c_size_t i = 0; i < raw->count; i++) {
                if (
                        book_runtime_table_key_equal(
                                &raw->entries[i].key,
                                key
                        ) == ISTRUE
                ) {
                        if (removed) *removed = raw->entries[i].value;

                        if (i + 1U < raw->count) {
                                memmove(
                                        &raw->entries[i],
                                        &raw->entries[i + 1U],
                                        (raw->count - i - 1U)
                                                * sizeof(*raw->entries)
                                );
                        }

                        raw->count--;
                        if (runtime->table_entries > 0) {
                                runtime->table_entries--;
                        }
                        return NORMAL;
                }
        }

        if (removed) *removed = book_value_nil_value();
        return NORMAL;
}

c_size_t book_runtime_table_length_value(const BOOK_VALUE * table) {
        if (
                !table
                || table->type != BOOK_VALUE_TABLE
                || !table->as.table
        ) {
                return 0;
        }

        return book_runtime_table_length(table->as.table);
}

c_size_t book_runtime_table_count_value(const BOOK_VALUE * table) {
        if (
                !table
                || table->type != BOOK_VALUE_TABLE
                || !table->as.table
        ) {
                return 0;
        }

        return table->as.table->count;
}

int book_runtime_table_entry_value(
        const BOOK_VALUE * table,
        c_size_t index,
        BOOK_VALUE * key,
        BOOK_VALUE * value
) {
        if (
                !table
                || table->type != BOOK_VALUE_TABLE
                || !table->as.table
                || index >= table->as.table->count
                || !key
                || !value
        ) {
                return ABNORMAL;
        }

        *key = table->as.table->entries[index].key;
        *value = table->as.table->entries[index].value;
        return NORMAL;
}

uint64_t book_runtime_random_u64(BOOK_RUNTIME * runtime) {
        uint64_t value;

        if (!runtime) return 0;

        value = runtime->prng_state;
        if (value == 0) value = 0x9e3779b97f4a7c15ULL;

        value ^= value >> 12;
        value ^= value << 25;
        value ^= value >> 27;

        runtime->prng_state = value;
        return value * 2685821657736338717ULL;
}

const char * book_value_type_name(book_value_type_t type) {
        switch (type) {
                case BOOK_VALUE_NIL:             return "nil";
                case BOOK_VALUE_BOOLEAN:         return "boolean";
                case BOOK_VALUE_NUMBER:          return "number";
                case BOOK_VALUE_STRING:          return "string";
                case BOOK_VALUE_TABLE:           return "table";
                case BOOK_VALUE_FUNCTION:        return "function";
                case BOOK_VALUE_NATIVE_FUNCTION: return "native-function";
                default:                         return "unknown";
        }
}
