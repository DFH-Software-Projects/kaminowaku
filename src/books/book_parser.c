// Copyright 2026 Jamison A. Drapeau
#include "book_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BOOK_PARSE_STOP_END     (1U << 0)
#define BOOK_PARSE_STOP_ELSEIF  (1U << 1)
#define BOOK_PARSE_STOP_ELSE    (1U << 2)

typedef struct BOOK_PARSER_STATE {
        BOOK_PROGRAM * program;
        c_size_t       current;
        unsigned int   loop_depth;
        int8_t         vararg_allowed;
} BOOK_PARSER_STATE;

static int book_parser_expression(
        BOOK_PARSER_STATE * state,
        int minimum_precedence,
        uint32_t * node_id
);

static int book_parser_statement(
        BOOK_PARSER_STATE * state,
        uint32_t * node_id
);

static int book_parser_block(
        BOOK_PARSER_STATE * state,
        unsigned int stop_flags,
        uint32_t * first_statement
);

static int book_parser_fail_at(
        BOOK_PARSER_STATE * state,
        const BOOK_TOKEN * token,
        const char * message
) {
        if (!state || !state->program || !message) return ABNORMAL;

        if (state->program->status != ABNORMAL) {
                state->program->status = ABNORMAL;
                state->program->error_line = token ? token->line : 1;
                state->program->error_column = token ? token->column : 1;
                snprintf(
                        state->program->error,
                        sizeof(state->program->error),
                        "%s",
                        message
                );
        }

        return ABNORMAL;
}

static const BOOK_TOKEN * book_parser_current(
        const BOOK_PARSER_STATE * state
) {
        if (
                !state
                || !state->program
                || !state->program->lexer.tokens
                || state->current >= state->program->lexer.count
        ) {
                return NULL;
        }

        return &state->program->lexer.tokens[state->current];
}

static const BOOK_TOKEN * book_parser_peek(
        const BOOK_PARSER_STATE * state,
        c_size_t ahead
) {
        c_size_t index;

        if (!state || !state->program) return NULL;
        if (ahead > state->program->lexer.count) return NULL;
        if (state->current > state->program->lexer.count - ahead) return NULL;

        index = state->current + ahead;
        if (index >= state->program->lexer.count) return NULL;
        return &state->program->lexer.tokens[index];
}

static int book_parser_check(
        const BOOK_PARSER_STATE * state,
        book_token_kind_t kind
) {
        const BOOK_TOKEN * token = book_parser_current(state);

        return token && token->kind == kind ? ISTRUE : ISFALSE;
}

static int book_parser_match(
        BOOK_PARSER_STATE * state,
        book_token_kind_t kind
) {
        if (book_parser_check(state, kind) != ISTRUE) return ISFALSE;
        state->current++;
        return ISTRUE;
}

static int book_parser_expect(
        BOOK_PARSER_STATE * state,
        book_token_kind_t kind,
        const char * message
) {
        const BOOK_TOKEN * token = book_parser_current(state);

        if (!token || token->kind != kind) {
                return book_parser_fail_at(state, token, message);
        }

        state->current++;
        return NORMAL;
}

static int book_parser_node_reserve(BOOK_PARSER_STATE * state) {
        BOOK_AST_NODE * nodes;
        uint32_t next;

        if (!state || !state->program) return ABNORMAL;
        if (state->program->node_count < state->program->node_capacity) {
                return NORMAL;
        }

        if (state->program->node_count >= BOOK_PARSER_NODE_LIMIT) {
                return book_parser_fail_at(
                        state,
                        book_parser_current(state),
                        "AST node limit exceeded"
                );
        }

        next = state->program->node_capacity
                ? state->program->node_capacity * 2U
                : 256U;

        if (next > BOOK_PARSER_NODE_LIMIT) {
                next = BOOK_PARSER_NODE_LIMIT;
        }

        nodes = realloc(
                state->program->nodes,
                (c_size_t)(next + 1U) * sizeof(*nodes)
        );
        if (!nodes) {
                return book_parser_fail_at(
                        state,
                        book_parser_current(state),
                        "failed to allocate AST nodes"
                );
        }

        memset(
                nodes + state->program->node_capacity + 1U,
                0x00,
                (c_size_t)(next - state->program->node_capacity)
                        * sizeof(*nodes)
        );

        state->program->nodes = nodes;
        state->program->node_capacity = next;
        return NORMAL;
}

static uint32_t book_parser_node_new(
        BOOK_PARSER_STATE * state,
        book_ast_kind_t kind,
        c_size_t token
) {
        uint32_t id;

        if (!state || !state->program) return 0;
        if (book_parser_node_reserve(state) != NORMAL) return 0;

        id = ++state->program->node_count;
        memset(&state->program->nodes[id], 0x00, sizeof(state->program->nodes[id]));
        state->program->nodes[id].kind = kind;
        state->program->nodes[id].token = token;
        return id;
}

static int book_parser_list_reserve(BOOK_PARSER_STATE * state) {
        BOOK_AST_LIST_ENTRY * lists;
        uint32_t next;

        if (!state || !state->program) return ABNORMAL;
        if (state->program->list_count < state->program->list_capacity) {
                return NORMAL;
        }

        if (state->program->list_count >= BOOK_PARSER_LIST_LIMIT) {
                return book_parser_fail_at(
                        state,
                        book_parser_current(state),
                        "AST list limit exceeded"
                );
        }

        next = state->program->list_capacity
                ? state->program->list_capacity * 2U
                : 256U;

        if (next > BOOK_PARSER_LIST_LIMIT) {
                next = BOOK_PARSER_LIST_LIMIT;
        }

        lists = realloc(
                state->program->lists,
                (c_size_t)(next + 1U) * sizeof(*lists)
        );
        if (!lists) {
                return book_parser_fail_at(
                        state,
                        book_parser_current(state),
                        "failed to allocate AST lists"
                );
        }

        memset(
                lists + state->program->list_capacity + 1U,
                0x00,
                (c_size_t)(next - state->program->list_capacity)
                        * sizeof(*lists)
        );

        state->program->lists = lists;
        state->program->list_capacity = next;
        return NORMAL;
}

static int book_parser_list_append(
        BOOK_PARSER_STATE * state,
        uint32_t * head,
        uint32_t * tail,
        uint32_t node
) {
        uint32_t id;

        if (!state || !head || !tail || node == 0) return ABNORMAL;
        if (book_parser_list_reserve(state) != NORMAL) return ABNORMAL;

        id = ++state->program->list_count;
        state->program->lists[id].node = node;
        state->program->lists[id].next = 0;

        if (*tail) {
                state->program->lists[*tail].next = id;
        } else {
                *head = id;
        }

        *tail = id;
        return NORMAL;
}

static int book_parser_statement_append(
        BOOK_PARSER_STATE * state,
        uint32_t * first,
        uint32_t * last,
        uint32_t statement
) {
        if (!state || !first || !last || statement == 0) return ABNORMAL;

        if (*last) {
                state->program->nodes[*last].next = statement;
        } else {
                *first = statement;
        }

        *last = statement;
        return NORMAL;
}

static int book_parser_is_lvalue(
        const BOOK_PROGRAM * program,
        uint32_t node_id
) {
        const BOOK_AST_NODE * node;

        if (!program || node_id == 0 || node_id > program->node_count) {
                return ISFALSE;
        }

        node = &program->nodes[node_id];
        return (
                node->kind == BOOK_AST_VARIABLE
                || node->kind == BOOK_AST_FIELD
                || node->kind == BOOK_AST_INDEX
        ) ? ISTRUE : ISFALSE;
}

static int book_parser_binary_precedence(
        book_token_kind_t kind,
        int * right_associative
) {
        if (right_associative) *right_associative = ISFALSE;

        switch (kind) {
                case BOOK_TOKEN_OR:             return 1;
                case BOOK_TOKEN_AND:            return 2;

                case BOOK_TOKEN_LESS:
                case BOOK_TOKEN_LESS_EQUAL:
                case BOOK_TOKEN_GREATER:
                case BOOK_TOKEN_GREATER_EQUAL:
                case BOOK_TOKEN_EQUAL_EQUAL:
                case BOOK_TOKEN_NOT_EQUAL:      return 3;

                case BOOK_TOKEN_BIT_OR:          return 4;
                case BOOK_TOKEN_TILDE:           return 5;
                case BOOK_TOKEN_BIT_AND:         return 6;

                case BOOK_TOKEN_SHIFT_LEFT:
                case BOOK_TOKEN_SHIFT_RIGHT:    return 7;

                case BOOK_TOKEN_CONCAT:
                        if (right_associative) *right_associative = ISTRUE;
                        return 8;

                case BOOK_TOKEN_PLUS:
                case BOOK_TOKEN_MINUS:          return 9;

                case BOOK_TOKEN_STAR:
                case BOOK_TOKEN_SLASH:
                case BOOK_TOKEN_FLOOR_DIV:
                case BOOK_TOKEN_PERCENT:        return 10;

                default:                        return 0;
        }
}

static int book_parser_expression_list(
        BOOK_PARSER_STATE * state,
        uint32_t * head
) {
        uint32_t tail = 0;
        uint32_t expression;

        if (!state || !head) return ABNORMAL;
        *head = 0;

        if (book_parser_expression(state, 1, &expression) != NORMAL) {
                return ABNORMAL;
        }

        if (
                book_parser_list_append(
                        state,
                        head,
                        &tail,
                        expression
                ) != NORMAL
        ) {
                return ABNORMAL;
        }

        while (book_parser_match(state, BOOK_TOKEN_COMMA) == ISTRUE) {
                if (book_parser_expression(state, 1, &expression) != NORMAL) {
                        return ABNORMAL;
                }

                if (
                        book_parser_list_append(
                                state,
                                head,
                                &tail,
                                expression
                        ) != NORMAL
                ) {
                        return ABNORMAL;
                }
        }

        return NORMAL;
}

static int book_parser_table(BOOK_PARSER_STATE * state, uint32_t * node_id) {
        uint32_t table_id;
        uint32_t head = 0;
        uint32_t tail = 0;
        c_size_t table_token;

        if (!state || !node_id) return ABNORMAL;
        table_token = state->current;

        if (
                book_parser_expect(
                        state,
                        BOOK_TOKEN_LEFT_BRACE,
                        "expected '{'"
                ) != NORMAL
        ) {
                return ABNORMAL;
        }

        table_id = book_parser_node_new(
                state,
                BOOK_AST_TABLE,
                table_token
        );
        if (!table_id) return ABNORMAL;

        while (book_parser_check(state, BOOK_TOKEN_RIGHT_BRACE) != ISTRUE) {
                uint32_t field_id;
                uint32_t key = 0;
                uint32_t value = 0;
                uint32_t flags = BOOK_TABLE_FIELD_ARRAY;
                c_size_t token = state->current;
                const BOOK_TOKEN * current = book_parser_current(state);
                const BOOK_TOKEN * next = book_parser_peek(state, 1);

                if (!current || current->kind == BOOK_TOKEN_EOF) {
                        return book_parser_fail_at(
                                state,
                                current,
                                "unterminated table constructor"
                        );
                }

                if (book_parser_match(state, BOOK_TOKEN_LEFT_BRACKET) == ISTRUE) {
                        flags = BOOK_TABLE_FIELD_INDEXED;

                        if (book_parser_expression(state, 1, &key) != NORMAL) {
                                return ABNORMAL;
                        }

                        if (
                                book_parser_expect(
                                        state,
                                        BOOK_TOKEN_RIGHT_BRACKET,
                                        "expected ']' in table field"
                                ) != NORMAL
                        ) {
                                return ABNORMAL;
                        }

                        if (
                                book_parser_expect(
                                        state,
                                        BOOK_TOKEN_EQUAL,
                                        "expected '=' after table key"
                                ) != NORMAL
                        ) {
                                return ABNORMAL;
                        }

                        if (book_parser_expression(state, 1, &value) != NORMAL) {
                                return ABNORMAL;
                        }
                } else if (
                        current->kind == BOOK_TOKEN_IDENTIFIER
                        && next
                        && next->kind == BOOK_TOKEN_EQUAL
                ) {
                        flags = BOOK_TABLE_FIELD_NAMED;
                        token = state->current;
                        state->current += 2;

                        if (book_parser_expression(state, 1, &value) != NORMAL) {
                                return ABNORMAL;
                        }
                } else {
                        if (book_parser_expression(state, 1, &value) != NORMAL) {
                                return ABNORMAL;
                        }
                }

                field_id = book_parser_node_new(
                        state,
                        BOOK_AST_TABLE_FIELD,
                        token
                );
                if (!field_id) return ABNORMAL;

                state->program->nodes[field_id].a = key;
                state->program->nodes[field_id].b = value;
                state->program->nodes[field_id].flags = flags;

                if (
                        book_parser_list_append(
                                state,
                                &head,
                                &tail,
                                field_id
                        ) != NORMAL
                ) {
                        return ABNORMAL;
                }

                if (
                        book_parser_match(state, BOOK_TOKEN_COMMA) == ISTRUE
                        || book_parser_match(state, BOOK_TOKEN_SEMICOLON) == ISTRUE
                ) {
                        if (book_parser_check(state, BOOK_TOKEN_RIGHT_BRACE) == ISTRUE) {
                                break;
                        }
                        continue;
                }

                if (book_parser_check(state, BOOK_TOKEN_RIGHT_BRACE) != ISTRUE) {
                        return book_parser_fail_at(
                                state,
                                book_parser_current(state),
                                "expected ',' ';' or '}' in table constructor"
                        );
                }
        }

        if (
                book_parser_expect(
                        state,
                        BOOK_TOKEN_RIGHT_BRACE,
                        "expected '}'"
                ) != NORMAL
        ) {
                return ABNORMAL;
        }

        state->program->nodes[table_id].list1 = head;
        *node_id = table_id;
        return NORMAL;
}

static int book_parser_function_body(
        BOOK_PARSER_STATE * state,
        c_size_t function_token,
        uint32_t * node_id
) {
        uint32_t function_id;
        uint32_t parameters = 0;
        uint32_t parameter_tail = 0;
        uint32_t body = 0;
        uint32_t flags = 0;
        unsigned int saved_loop_depth;
        int8_t saved_vararg;

        if (!state || !node_id) return ABNORMAL;

        if (
                book_parser_expect(
                        state,
                        BOOK_TOKEN_LEFT_PAREN,
                        "expected '(' after function name"
                ) != NORMAL
        ) {
                return ABNORMAL;
        }

        if (book_parser_check(state, BOOK_TOKEN_RIGHT_PAREN) != ISTRUE) {
                for (;;) {
                        uint32_t parameter;
                        c_size_t token;

                        if (book_parser_match(state, BOOK_TOKEN_ELLIPSIS) == ISTRUE) {
                                flags |= BOOK_AST_FUNCTION_VARARG;
                                break;
                        }

                        if (book_parser_check(state, BOOK_TOKEN_IDENTIFIER) != ISTRUE) {
                                return book_parser_fail_at(
                                        state,
                                        book_parser_current(state),
                                        "expected function parameter"
                                );
                        }

                        token = state->current++;
                        parameter = book_parser_node_new(
                                state,
                                BOOK_AST_VARIABLE,
                                token
                        );
                        if (!parameter) return ABNORMAL;

                        if (
                                book_parser_list_append(
                                        state,
                                        &parameters,
                                        &parameter_tail,
                                        parameter
                                ) != NORMAL
                        ) {
                                return ABNORMAL;
                        }

                        if (book_parser_match(state, BOOK_TOKEN_COMMA) != ISTRUE) {
                                break;
                        }

                        if (book_parser_check(state, BOOK_TOKEN_ELLIPSIS) == ISTRUE) {
                                state->current++;
                                flags |= BOOK_AST_FUNCTION_VARARG;
                                break;
                        }
                }
        }

        if (
                book_parser_expect(
                        state,
                        BOOK_TOKEN_RIGHT_PAREN,
                        "expected ')' after function parameters"
                ) != NORMAL
        ) {
                return ABNORMAL;
        }

        saved_loop_depth = state->loop_depth;
        saved_vararg = state->vararg_allowed;
        state->loop_depth = 0;
        state->vararg_allowed = (flags & BOOK_AST_FUNCTION_VARARG) ? ISTRUE : ISFALSE;

        if (
                book_parser_block(
                        state,
                        BOOK_PARSE_STOP_END,
                        &body
                ) != NORMAL
        ) {
                state->loop_depth = saved_loop_depth;
                state->vararg_allowed = saved_vararg;
                return ABNORMAL;
        }

        state->loop_depth = saved_loop_depth;
        state->vararg_allowed = saved_vararg;

        if (
                book_parser_expect(
                        state,
                        BOOK_TOKEN_END,
                        "expected 'end' after function body"
                ) != NORMAL
        ) {
                return ABNORMAL;
        }

        function_id = book_parser_node_new(
                state,
                BOOK_AST_FUNCTION,
                function_token
        );
        if (!function_id) return ABNORMAL;

        state->program->nodes[function_id].list1 = parameters;
        state->program->nodes[function_id].a = body;
        state->program->nodes[function_id].flags = flags;
        *node_id = function_id;
        return NORMAL;
}

static int book_parser_call_arguments(
        BOOK_PARSER_STATE * state,
        uint32_t * arguments
) {
        uint32_t head = 0;
        uint32_t tail = 0;

        if (!state || !arguments) return ABNORMAL;
        *arguments = 0;

        if (book_parser_match(state, BOOK_TOKEN_LEFT_PAREN) == ISTRUE) {
                if (book_parser_check(state, BOOK_TOKEN_RIGHT_PAREN) != ISTRUE) {
                        if (book_parser_expression_list(state, &head) != NORMAL) {
                                return ABNORMAL;
                        }
                }

                if (
                        book_parser_expect(
                                state,
                                BOOK_TOKEN_RIGHT_PAREN,
                                "expected ')' after call arguments"
                        ) != NORMAL
                ) {
                        return ABNORMAL;
                }

                *arguments = head;
                return NORMAL;
        }

        if (book_parser_check(state, BOOK_TOKEN_LEFT_BRACE) == ISTRUE) {
                uint32_t table;

                if (book_parser_table(state, &table) != NORMAL) return ABNORMAL;
                if (
                        book_parser_list_append(
                                state,
                                &head,
                                &tail,
                                table
                        ) != NORMAL
                ) {
                        return ABNORMAL;
                }

                *arguments = head;
                return NORMAL;
        }

        if (book_parser_check(state, BOOK_TOKEN_STRING) == ISTRUE) {
                uint32_t string;
                c_size_t token = state->current++;

                string = book_parser_node_new(
                        state,
                        BOOK_AST_STRING,
                        token
                );
                if (!string) return ABNORMAL;

                if (
                        book_parser_list_append(
                                state,
                                &head,
                                &tail,
                                string
                        ) != NORMAL
                ) {
                        return ABNORMAL;
                }

                *arguments = head;
                return NORMAL;
        }

        return book_parser_fail_at(
                state,
                book_parser_current(state),
                "expected call arguments"
        );
}

static int book_parser_primary(
        BOOK_PARSER_STATE * state,
        uint32_t * node_id
) {
        const BOOK_TOKEN * token;
        uint32_t node = 0;
        c_size_t token_index;

        if (!state || !node_id) return ABNORMAL;
        token = book_parser_current(state);
        if (!token) return book_parser_fail_at(state, token, "expected expression");
        token_index = state->current;

        switch (token->kind) {
                case BOOK_TOKEN_NIL:
                        state->current++;
                        node = book_parser_node_new(
                                state,
                                BOOK_AST_NIL,
                                token_index
                        );
                        break;

                case BOOK_TOKEN_TRUE:
                case BOOK_TOKEN_FALSE:
                        state->current++;
                        node = book_parser_node_new(
                                state,
                                BOOK_AST_BOOLEAN,
                                token_index
                        );
                        if (node) {
                                state->program->nodes[node].flags =
                                        token->kind == BOOK_TOKEN_TRUE
                                                ? ISTRUE
                                                : ISFALSE;
                        }
                        break;

                case BOOK_TOKEN_NUMBER:
                        state->current++;
                        node = book_parser_node_new(
                                state,
                                BOOK_AST_NUMBER,
                                token_index
                        );
                        break;

                case BOOK_TOKEN_STRING:
                        state->current++;
                        node = book_parser_node_new(
                                state,
                                BOOK_AST_STRING,
                                token_index
                        );
                        break;

                case BOOK_TOKEN_IDENTIFIER:
                        state->current++;
                        node = book_parser_node_new(
                                state,
                                BOOK_AST_VARIABLE,
                                token_index
                        );
                        break;

                case BOOK_TOKEN_ELLIPSIS:
                        if (state->vararg_allowed != ISTRUE) {
                                return book_parser_fail_at(
                                        state,
                                        token,
                                        "'...' is only valid inside a vararg function"
                                );
                        }
                        state->current++;
                        node = book_parser_node_new(
                                state,
                                BOOK_AST_VARARG,
                                token_index
                        );
                        break;

                case BOOK_TOKEN_LEFT_BRACE:
                        if (book_parser_table(state, &node) != NORMAL) {
                                return ABNORMAL;
                        }
                        break;

                case BOOK_TOKEN_FUNCTION:
                        state->current++;
                        if (
                                book_parser_function_body(
                                        state,
                                        token_index,
                                        &node
                                ) != NORMAL
                        ) {
                                return ABNORMAL;
                        }
                        break;

                case BOOK_TOKEN_LEFT_PAREN:
                        state->current++;
                        if (book_parser_expression(state, 1, &node) != NORMAL) {
                                return ABNORMAL;
                        }
                        if (
                                book_parser_expect(
                                        state,
                                        BOOK_TOKEN_RIGHT_PAREN,
                                        "expected ')' after expression"
                                ) != NORMAL
                        ) {
                                return ABNORMAL;
                        }
                        break;

                default:
                        return book_parser_fail_at(
                                state,
                                token,
                                "expected expression"
                        );
        }

        if (!node) return ABNORMAL;

        for (;;) {
                if (book_parser_match(state, BOOK_TOKEN_DOT) == ISTRUE) {
                        uint32_t field;
                        c_size_t field_token;

                        if (book_parser_check(state, BOOK_TOKEN_IDENTIFIER) != ISTRUE) {
                                return book_parser_fail_at(
                                        state,
                                        book_parser_current(state),
                                        "expected field name after '.'"
                                );
                        }

                        field_token = state->current++;
                        field = book_parser_node_new(
                                state,
                                BOOK_AST_FIELD,
                                field_token
                        );
                        if (!field) return ABNORMAL;

                        state->program->nodes[field].a = node;
                        node = field;
                        continue;
                }

                if (book_parser_match(state, BOOK_TOKEN_LEFT_BRACKET) == ISTRUE) {
                        uint32_t index;
                        uint32_t key;

                        if (book_parser_expression(state, 1, &key) != NORMAL) {
                                return ABNORMAL;
                        }

                        if (
                                book_parser_expect(
                                        state,
                                        BOOK_TOKEN_RIGHT_BRACKET,
                                        "expected ']' after index"
                                ) != NORMAL
                        ) {
                                return ABNORMAL;
                        }

                        index = book_parser_node_new(
                                state,
                                BOOK_AST_INDEX,
                                token_index
                        );
                        if (!index) return ABNORMAL;

                        state->program->nodes[index].a = node;
                        state->program->nodes[index].b = key;
                        node = index;
                        continue;
                }

                if (
                        book_parser_check(state, BOOK_TOKEN_LEFT_PAREN) == ISTRUE
                        || book_parser_check(state, BOOK_TOKEN_LEFT_BRACE) == ISTRUE
                        || book_parser_check(state, BOOK_TOKEN_STRING) == ISTRUE
                ) {
                        uint32_t call;
                        uint32_t arguments;

                        if (
                                book_parser_call_arguments(
                                        state,
                                        &arguments
                                ) != NORMAL
                        ) {
                                return ABNORMAL;
                        }

                        call = book_parser_node_new(
                                state,
                                BOOK_AST_CALL,
                                token_index
                        );
                        if (!call) return ABNORMAL;

                        state->program->nodes[call].a = node;
                        state->program->nodes[call].list1 = arguments;
                        node = call;
                        continue;
                }

                break;
        }

        *node_id = node;
        return NORMAL;
}

static int book_parser_prefix(
        BOOK_PARSER_STATE * state,
        uint32_t * node_id
) {
        const BOOK_TOKEN * token;
        c_size_t token_index;

        if (!state || !node_id) return ABNORMAL;
        token = book_parser_current(state);
        if (!token) return book_parser_fail_at(state, token, "expected expression");

        if (
                token->kind == BOOK_TOKEN_NOT
                || token->kind == BOOK_TOKEN_MINUS
                || token->kind == BOOK_TOKEN_LENGTH
                || token->kind == BOOK_TOKEN_TILDE
        ) {
                uint32_t operand;
                uint32_t unary;

                token_index = state->current++;
                if (book_parser_expression(state, 11, &operand) != NORMAL) {
                        return ABNORMAL;
                }

                unary = book_parser_node_new(
                        state,
                        BOOK_AST_UNARY,
                        token_index
                );
                if (!unary) return ABNORMAL;

                state->program->nodes[unary].op = token->kind;
                state->program->nodes[unary].a = operand;
                *node_id = unary;
                return NORMAL;
        }

        return book_parser_primary(state, node_id);
}

static int book_parser_expression(
        BOOK_PARSER_STATE * state,
        int minimum_precedence,
        uint32_t * node_id
) {
        uint32_t left;

        if (!state || !node_id) return ABNORMAL;
        if (book_parser_prefix(state, &left) != NORMAL) return ABNORMAL;

        for (;;) {
                const BOOK_TOKEN * token = book_parser_current(state);
                int right_associative = ISFALSE;
                int precedence;
                uint32_t right;
                uint32_t binary;
                c_size_t operator_token;

                if (!token) break;
                precedence = book_parser_binary_precedence(
                        token->kind,
                        &right_associative
                );

                if (precedence < minimum_precedence || precedence == 0) {
                        break;
                }

                operator_token = state->current++;
                if (
                        book_parser_expression(
                                state,
                                right_associative == ISTRUE
                                        ? precedence
                                        : precedence + 1,
                                &right
                        ) != NORMAL
                ) {
                        return ABNORMAL;
                }

                binary = book_parser_node_new(
                        state,
                        BOOK_AST_BINARY,
                        operator_token
                );
                if (!binary) return ABNORMAL;

                state->program->nodes[binary].op = token->kind;
                state->program->nodes[binary].a = left;
                state->program->nodes[binary].b = right;
                left = binary;
        }

        *node_id = left;
        return NORMAL;
}

static int book_parser_local_statement(
        BOOK_PARSER_STATE * state,
        uint32_t * node_id
) {
        c_size_t local_token;

        if (!state || !node_id) return ABNORMAL;
        local_token = state->current - 1U;

        if (book_parser_match(state, BOOK_TOKEN_FUNCTION) == ISTRUE) {
                uint32_t statement;
                uint32_t function;
                c_size_t name_token;

                if (book_parser_check(state, BOOK_TOKEN_IDENTIFIER) != ISTRUE) {
                        return book_parser_fail_at(
                                state,
                                book_parser_current(state),
                                "expected local function name"
                        );
                }

                name_token = state->current++;
                if (
                        book_parser_function_body(
                                state,
                                local_token,
                                &function
                        ) != NORMAL
                ) {
                        return ABNORMAL;
                }

                statement = book_parser_node_new(
                        state,
                        BOOK_AST_LOCAL_FUNCTION,
                        local_token
                );
                if (!statement) return ABNORMAL;

                state->program->nodes[statement].token = name_token;
                state->program->nodes[statement].a = function;
                *node_id = statement;
                return NORMAL;
        }

        {
                uint32_t statement;
                uint32_t names = 0;
                uint32_t names_tail = 0;
                uint32_t values = 0;

                for (;;) {
                        uint32_t variable;
                        c_size_t name_token;

                        if (book_parser_check(state, BOOK_TOKEN_IDENTIFIER) != ISTRUE) {
                                return book_parser_fail_at(
                                        state,
                                        book_parser_current(state),
                                        "expected local variable name"
                                );
                        }

                        name_token = state->current++;
                        variable = book_parser_node_new(
                                state,
                                BOOK_AST_VARIABLE,
                                name_token
                        );
                        if (!variable) return ABNORMAL;

                        if (
                                book_parser_list_append(
                                        state,
                                        &names,
                                        &names_tail,
                                        variable
                                ) != NORMAL
                        ) {
                                return ABNORMAL;
                        }

                        if (book_parser_match(state, BOOK_TOKEN_COMMA) != ISTRUE) {
                                break;
                        }
                }

                if (book_parser_match(state, BOOK_TOKEN_EQUAL) == ISTRUE) {
                        if (book_parser_expression_list(state, &values) != NORMAL) {
                                return ABNORMAL;
                        }
                }

                statement = book_parser_node_new(
                        state,
                        BOOK_AST_LOCAL_ASSIGN,
                        local_token
                );
                if (!statement) return ABNORMAL;

                state->program->nodes[statement].list1 = names;
                state->program->nodes[statement].list2 = values;
                *node_id = statement;
                return NORMAL;
        }
}

static int book_parser_function_statement(
        BOOK_PARSER_STATE * state,
        uint32_t * node_id
) {
        uint32_t target;
        uint32_t function;
        uint32_t statement;
        c_size_t function_token;
        c_size_t name_token;

        if (!state || !node_id) return ABNORMAL;
        function_token = state->current - 1U;

        if (book_parser_check(state, BOOK_TOKEN_IDENTIFIER) != ISTRUE) {
                return book_parser_fail_at(
                        state,
                        book_parser_current(state),
                        "expected function name"
                );
        }

        name_token = state->current++;
        target = book_parser_node_new(
                state,
                BOOK_AST_VARIABLE,
                name_token
        );
        if (!target) return ABNORMAL;

        while (book_parser_match(state, BOOK_TOKEN_DOT) == ISTRUE) {
                uint32_t field;
                c_size_t field_token;

                if (book_parser_check(state, BOOK_TOKEN_IDENTIFIER) != ISTRUE) {
                        return book_parser_fail_at(
                                state,
                                book_parser_current(state),
                                "expected function field name"
                        );
                }

                field_token = state->current++;
                field = book_parser_node_new(
                        state,
                        BOOK_AST_FIELD,
                        field_token
                );
                if (!field) return ABNORMAL;
                state->program->nodes[field].a = target;
                target = field;
        }

        if (
                book_parser_function_body(
                        state,
                        function_token,
                        &function
                ) != NORMAL
        ) {
                return ABNORMAL;
        }

        statement = book_parser_node_new(
                state,
                BOOK_AST_FUNCTION_DECL,
                function_token
        );
        if (!statement) return ABNORMAL;

        state->program->nodes[statement].a = target;
        state->program->nodes[statement].b = function;
        *node_id = statement;
        return NORMAL;
}

static int book_parser_if_statement(
        BOOK_PARSER_STATE * state,
        uint32_t * node_id
) {
        uint32_t statement;
        uint32_t clauses = 0;
        uint32_t clauses_tail = 0;
        c_size_t if_token;

        if (!state || !node_id) return ABNORMAL;
        if_token = state->current - 1U;

        for (;;) {
                uint32_t condition;
                uint32_t body;
                uint32_t clause;
                c_size_t clause_token = state->current;

                if (book_parser_expression(state, 1, &condition) != NORMAL) {
                        return ABNORMAL;
                }

                if (
                        book_parser_expect(
                                state,
                                BOOK_TOKEN_THEN,
                                "expected 'then' after if condition"
                        ) != NORMAL
                ) {
                        return ABNORMAL;
                }

                if (
                        book_parser_block(
                                state,
                                BOOK_PARSE_STOP_END
                                        | BOOK_PARSE_STOP_ELSEIF
                                        | BOOK_PARSE_STOP_ELSE,
                                &body
                        ) != NORMAL
                ) {
                        return ABNORMAL;
                }

                clause = book_parser_node_new(
                        state,
                        BOOK_AST_IF_CLAUSE,
                        clause_token
                );
                if (!clause) return ABNORMAL;

                state->program->nodes[clause].a = condition;
                state->program->nodes[clause].b = body;

                if (
                        book_parser_list_append(
                                state,
                                &clauses,
                                &clauses_tail,
                                clause
                        ) != NORMAL
                ) {
                        return ABNORMAL;
                }

                if (book_parser_match(state, BOOK_TOKEN_ELSEIF) == ISTRUE) {
                        continue;
                }

                if (book_parser_match(state, BOOK_TOKEN_ELSE) == ISTRUE) {
                        uint32_t else_body;
                        uint32_t else_clause;

                        if (
                                book_parser_block(
                                        state,
                                        BOOK_PARSE_STOP_END,
                                        &else_body
                                ) != NORMAL
                        ) {
                                return ABNORMAL;
                        }

                        else_clause = book_parser_node_new(
                                state,
                                BOOK_AST_IF_CLAUSE,
                                state->current
                        );
                        if (!else_clause) return ABNORMAL;

                        state->program->nodes[else_clause].a = 0;
                        state->program->nodes[else_clause].b = else_body;

                        if (
                                book_parser_list_append(
                                        state,
                                        &clauses,
                                        &clauses_tail,
                                        else_clause
                                ) != NORMAL
                        ) {
                                return ABNORMAL;
                        }
                }

                break;
        }

        if (
                book_parser_expect(
                        state,
                        BOOK_TOKEN_END,
                        "expected 'end' after if statement"
                ) != NORMAL
        ) {
                return ABNORMAL;
        }

        statement = book_parser_node_new(
                state,
                BOOK_AST_IF,
                if_token
        );
        if (!statement) return ABNORMAL;

        state->program->nodes[statement].list1 = clauses;
        *node_id = statement;
        return NORMAL;
}

static int book_parser_while_statement(
        BOOK_PARSER_STATE * state,
        uint32_t * node_id
) {
        uint32_t condition;
        uint32_t body;
        uint32_t statement;
        c_size_t while_token;

        if (!state || !node_id) return ABNORMAL;
        while_token = state->current - 1U;

        if (book_parser_expression(state, 1, &condition) != NORMAL) {
                return ABNORMAL;
        }

        if (
                book_parser_expect(
                        state,
                        BOOK_TOKEN_DO,
                        "expected 'do' after while condition"
                ) != NORMAL
        ) {
                return ABNORMAL;
        }

        state->loop_depth++;
        if (
                book_parser_block(
                        state,
                        BOOK_PARSE_STOP_END,
                        &body
                ) != NORMAL
        ) {
                state->loop_depth--;
                return ABNORMAL;
        }
        state->loop_depth--;

        if (
                book_parser_expect(
                        state,
                        BOOK_TOKEN_END,
                        "expected 'end' after while statement"
                ) != NORMAL
        ) {
                return ABNORMAL;
        }

        statement = book_parser_node_new(
                state,
                BOOK_AST_WHILE,
                while_token
        );
        if (!statement) return ABNORMAL;

        state->program->nodes[statement].a = condition;
        state->program->nodes[statement].b = body;
        *node_id = statement;
        return NORMAL;
}

static int book_parser_for_statement(
        BOOK_PARSER_STATE * state,
        uint32_t * node_id
) {
        c_size_t for_token;
        c_size_t first_name;

        if (!state || !node_id) return ABNORMAL;
        for_token = state->current - 1U;

        if (book_parser_check(state, BOOK_TOKEN_IDENTIFIER) != ISTRUE) {
                return book_parser_fail_at(
                        state,
                        book_parser_current(state),
                        "expected for-loop variable"
                );
        }

        first_name = state->current++;

        if (book_parser_match(state, BOOK_TOKEN_EQUAL) == ISTRUE) {
                uint32_t first;
                uint32_t last;
                uint32_t step = 0;
                uint32_t body;
                uint32_t statement;

                if (book_parser_expression(state, 1, &first) != NORMAL) {
                        return ABNORMAL;
                }

                if (
                        book_parser_expect(
                                state,
                                BOOK_TOKEN_COMMA,
                                "expected ',' in numeric for loop"
                        ) != NORMAL
                ) {
                        return ABNORMAL;
                }

                if (book_parser_expression(state, 1, &last) != NORMAL) {
                        return ABNORMAL;
                }

                if (book_parser_match(state, BOOK_TOKEN_COMMA) == ISTRUE) {
                        if (book_parser_expression(state, 1, &step) != NORMAL) {
                                return ABNORMAL;
                        }
                }

                if (
                        book_parser_expect(
                                state,
                                BOOK_TOKEN_DO,
                                "expected 'do' in numeric for loop"
                        ) != NORMAL
                ) {
                        return ABNORMAL;
                }

                state->loop_depth++;
                if (
                        book_parser_block(
                                state,
                                BOOK_PARSE_STOP_END,
                                &body
                        ) != NORMAL
                ) {
                        state->loop_depth--;
                        return ABNORMAL;
                }
                state->loop_depth--;

                if (
                        book_parser_expect(
                                state,
                                BOOK_TOKEN_END,
                                "expected 'end' after numeric for loop"
                        ) != NORMAL
                ) {
                        return ABNORMAL;
                }

                statement = book_parser_node_new(
                        state,
                        BOOK_AST_FOR_NUMERIC,
                        for_token
                );
                if (!statement) return ABNORMAL;

                state->program->nodes[statement].token = first_name;
                state->program->nodes[statement].a = first;
                state->program->nodes[statement].b = last;
                state->program->nodes[statement].c = step;
                state->program->nodes[statement].d = body;
                *node_id = statement;
                return NORMAL;
        }

        {
                uint32_t names = 0;
                uint32_t names_tail = 0;
                uint32_t values = 0;
                uint32_t body;
                uint32_t statement;
                uint32_t variable;

                variable = book_parser_node_new(
                        state,
                        BOOK_AST_VARIABLE,
                        first_name
                );
                if (!variable) return ABNORMAL;

                if (
                        book_parser_list_append(
                                state,
                                &names,
                                &names_tail,
                                variable
                        ) != NORMAL
                ) {
                        return ABNORMAL;
                }

                while (book_parser_match(state, BOOK_TOKEN_COMMA) == ISTRUE) {
                        c_size_t name_token;

                        if (book_parser_check(state, BOOK_TOKEN_IDENTIFIER) != ISTRUE) {
                                return book_parser_fail_at(
                                        state,
                                        book_parser_current(state),
                                        "expected generic for-loop variable"
                                );
                        }

                        name_token = state->current++;
                        variable = book_parser_node_new(
                                state,
                                BOOK_AST_VARIABLE,
                                name_token
                        );
                        if (!variable) return ABNORMAL;

                        if (
                                book_parser_list_append(
                                        state,
                                        &names,
                                        &names_tail,
                                        variable
                                ) != NORMAL
                        ) {
                                return ABNORMAL;
                        }
                }

                if (
                        book_parser_expect(
                                state,
                                BOOK_TOKEN_IN,
                                "expected 'in' in generic for loop"
                        ) != NORMAL
                ) {
                        return ABNORMAL;
                }

                if (book_parser_expression_list(state, &values) != NORMAL) {
                        return ABNORMAL;
                }

                if (
                        book_parser_expect(
                                state,
                                BOOK_TOKEN_DO,
                                "expected 'do' in generic for loop"
                        ) != NORMAL
                ) {
                        return ABNORMAL;
                }

                state->loop_depth++;
                if (
                        book_parser_block(
                                state,
                                BOOK_PARSE_STOP_END,
                                &body
                        ) != NORMAL
                ) {
                        state->loop_depth--;
                        return ABNORMAL;
                }
                state->loop_depth--;

                if (
                        book_parser_expect(
                                state,
                                BOOK_TOKEN_END,
                                "expected 'end' after generic for loop"
                        ) != NORMAL
                ) {
                        return ABNORMAL;
                }

                statement = book_parser_node_new(
                        state,
                        BOOK_AST_FOR_GENERIC,
                        for_token
                );
                if (!statement) return ABNORMAL;

                state->program->nodes[statement].list1 = names;
                state->program->nodes[statement].list2 = values;
                state->program->nodes[statement].a = body;
                *node_id = statement;
                return NORMAL;
        }
}

static int book_parser_return_statement(
        BOOK_PARSER_STATE * state,
        uint32_t * node_id
) {
        uint32_t statement;
        uint32_t values = 0;
        c_size_t return_token;
        const BOOK_TOKEN * token;

        if (!state || !node_id) return ABNORMAL;
        return_token = state->current - 1U;
        token = book_parser_current(state);

        if (
                token
                && token->kind != BOOK_TOKEN_EOF
                && token->kind != BOOK_TOKEN_END
                && token->kind != BOOK_TOKEN_ELSE
                && token->kind != BOOK_TOKEN_ELSEIF
                && token->kind != BOOK_TOKEN_SEMICOLON
        ) {
                if (book_parser_expression_list(state, &values) != NORMAL) {
                        return ABNORMAL;
                }
        }

        (void)book_parser_match(state, BOOK_TOKEN_SEMICOLON);

        statement = book_parser_node_new(
                state,
                BOOK_AST_RETURN,
                return_token
        );
        if (!statement) return ABNORMAL;

        state->program->nodes[statement].list1 = values;
        *node_id = statement;
        return NORMAL;
}

static int book_parser_expression_statement(
        BOOK_PARSER_STATE * state,
        uint32_t * node_id
) {
        uint32_t first;
        c_size_t start_token;

        if (!state || !node_id) return ABNORMAL;
        start_token = state->current;

        if (book_parser_expression(state, 1, &first) != NORMAL) {
                return ABNORMAL;
        }

        if (
                book_parser_check(state, BOOK_TOKEN_EQUAL) == ISTRUE
                || book_parser_check(state, BOOK_TOKEN_COMMA) == ISTRUE
        ) {
                uint32_t left = 0;
                uint32_t left_tail = 0;
                uint32_t right = 0;
                uint32_t statement;

                if (
                        book_parser_is_lvalue(
                                state->program,
                                first
                        ) != ISTRUE
                ) {
                        return book_parser_fail_at(
                                state,
                                &state->program->lexer.tokens[start_token],
                                "left side of assignment is not assignable"
                        );
                }

                if (
                        book_parser_list_append(
                                state,
                                &left,
                                &left_tail,
                                first
                        ) != NORMAL
                ) {
                        return ABNORMAL;
                }

                while (book_parser_match(state, BOOK_TOKEN_COMMA) == ISTRUE) {
                        uint32_t destination;

                        if (book_parser_expression(state, 1, &destination) != NORMAL) {
                                return ABNORMAL;
                        }

                        if (
                                book_parser_is_lvalue(
                                        state->program,
                                        destination
                                ) != ISTRUE
                        ) {
                                return book_parser_fail_at(
                                        state,
                                        book_parser_current(state),
                                        "left side of assignment is not assignable"
                                );
                        }

                        if (
                                book_parser_list_append(
                                        state,
                                        &left,
                                        &left_tail,
                                        destination
                                ) != NORMAL
                        ) {
                                return ABNORMAL;
                        }
                }

                if (
                        book_parser_expect(
                                state,
                                BOOK_TOKEN_EQUAL,
                                "expected '=' in assignment"
                        ) != NORMAL
                ) {
                        return ABNORMAL;
                }

                if (book_parser_expression_list(state, &right) != NORMAL) {
                        return ABNORMAL;
                }

                statement = book_parser_node_new(
                        state,
                        BOOK_AST_ASSIGN,
                        start_token
                );
                if (!statement) return ABNORMAL;

                state->program->nodes[statement].list1 = left;
                state->program->nodes[statement].list2 = right;
                *node_id = statement;
                return NORMAL;
        }

        if (
                first == 0
                || first > state->program->node_count
                || state->program->nodes[first].kind != BOOK_AST_CALL
        ) {
                return book_parser_fail_at(
                        state,
                        &state->program->lexer.tokens[start_token],
                        "only function calls may be used as expression statements"
                );
        }

        {
                uint32_t statement = book_parser_node_new(
                        state,
                        BOOK_AST_EXPR_STATEMENT,
                        start_token
                );
                if (!statement) return ABNORMAL;

                state->program->nodes[statement].a = first;
                *node_id = statement;
                return NORMAL;
        }
}

static int book_parser_statement(
        BOOK_PARSER_STATE * state,
        uint32_t * node_id
) {
        const BOOK_TOKEN * token;

        if (!state || !node_id) return ABNORMAL;
        *node_id = 0;
        token = book_parser_current(state);
        if (!token) return book_parser_fail_at(state, token, "expected statement");

        if (book_parser_match(state, BOOK_TOKEN_LOCAL) == ISTRUE) {
                return book_parser_local_statement(state, node_id);
        }

        if (book_parser_match(state, BOOK_TOKEN_FUNCTION) == ISTRUE) {
                return book_parser_function_statement(state, node_id);
        }

        if (book_parser_match(state, BOOK_TOKEN_IF) == ISTRUE) {
                return book_parser_if_statement(state, node_id);
        }

        if (book_parser_match(state, BOOK_TOKEN_WHILE) == ISTRUE) {
                return book_parser_while_statement(state, node_id);
        }

        if (book_parser_match(state, BOOK_TOKEN_FOR) == ISTRUE) {
                return book_parser_for_statement(state, node_id);
        }

        if (book_parser_match(state, BOOK_TOKEN_BREAK) == ISTRUE) {
                uint32_t statement;

                if (state->loop_depth == 0) {
                        return book_parser_fail_at(
                                state,
                                token,
                                "'break' is only valid inside a loop"
                        );
                }

                statement = book_parser_node_new(
                        state,
                        BOOK_AST_BREAK,
                        state->current - 1U
                );
                if (!statement) return ABNORMAL;

                *node_id = statement;
                return NORMAL;
        }

        if (book_parser_match(state, BOOK_TOKEN_RETURN) == ISTRUE) {
                return book_parser_return_statement(state, node_id);
        }

        return book_parser_expression_statement(state, node_id);
}

static int book_parser_should_stop(
        const BOOK_PARSER_STATE * state,
        unsigned int stop_flags
) {
        if (!state) return ISTRUE;

        if (
                (stop_flags & BOOK_PARSE_STOP_END)
                && book_parser_check(state, BOOK_TOKEN_END) == ISTRUE
        ) {
                return ISTRUE;
        }

        if (
                (stop_flags & BOOK_PARSE_STOP_ELSEIF)
                && book_parser_check(state, BOOK_TOKEN_ELSEIF) == ISTRUE
        ) {
                return ISTRUE;
        }

        if (
                (stop_flags & BOOK_PARSE_STOP_ELSE)
                && book_parser_check(state, BOOK_TOKEN_ELSE) == ISTRUE
        ) {
                return ISTRUE;
        }

        return ISFALSE;
}

static int book_parser_block(
        BOOK_PARSER_STATE * state,
        unsigned int stop_flags,
        uint32_t * first_statement
) {
        uint32_t first = 0;
        uint32_t last = 0;

        if (!state || !first_statement) return ABNORMAL;

        while (
                book_parser_check(state, BOOK_TOKEN_EOF) != ISTRUE
                && book_parser_should_stop(state, stop_flags) != ISTRUE
        ) {
                uint32_t statement;

                if (book_parser_match(state, BOOK_TOKEN_SEMICOLON) == ISTRUE) {
                        continue;
                }

                if (book_parser_statement(state, &statement) != NORMAL) {
                        return ABNORMAL;
                }

                if (!statement) {
                        return book_parser_fail_at(
                                state,
                                book_parser_current(state),
                                "parser produced empty statement"
                        );
                }

                if (
                        book_parser_statement_append(
                                state,
                                &first,
                                &last,
                                statement
                        ) != NORMAL
                ) {
                        return ABNORMAL;
                }

                (void)book_parser_match(state, BOOK_TOKEN_SEMICOLON);
        }

        *first_statement = first;
        return NORMAL;
}

int book_parser_parse(
        BOOK_LEXER_RESULT * lexer,
        BOOK_PROGRAM * program
) {
        BOOK_PARSER_STATE state;
        uint32_t root = 0;

        if (!lexer || !program) return ABNORMAL;
        memset(program, 0x00, sizeof(*program));

        program->lexer = *lexer;
        memset(lexer, 0x00, sizeof(*lexer));

        if (
                program->lexer.status != NORMAL
                || !program->lexer.tokens
                || program->lexer.count == 0
        ) {
                program->status = ABNORMAL;
                program->error_line = program->lexer.error_line;
                program->error_column = program->lexer.error_column;
                snprintf(
                        program->error,
                        sizeof(program->error),
                        "%s",
                        program->lexer.error[0]
                                ? program->lexer.error
                                : "lexer result is invalid"
                );
                return ABNORMAL;
        }

        memset(&state, 0x00, sizeof(state));
        state.program = program;
        state.vararg_allowed = ISFALSE;
        program->status = NORMAL;

        if (book_parser_block(&state, 0, &root) != NORMAL) {
                program->status = ABNORMAL;
                return ABNORMAL;
        }

        if (
                book_parser_expect(
                        &state,
                        BOOK_TOKEN_EOF,
                        "expected end of Lua source"
                ) != NORMAL
        ) {
                program->status = ABNORMAL;
                return ABNORMAL;
        }

        program->root = root;
        program->status = NORMAL;
        return NORMAL;
}

int book_parser_parse_file(
        const unsigned char * path,
        BOOK_PROGRAM * program
) {
        BOOK_LEXER_RESULT lexer;

        if (!path || !program) return ABNORMAL;
        memset(&lexer, 0x00, sizeof(lexer));
        memset(program, 0x00, sizeof(*program));

        if (book_lexer_tokenize_file(path, &lexer) != NORMAL) {
                program->lexer = lexer;
                program->status = ABNORMAL;
                program->error_line = lexer.error_line;
                program->error_column = lexer.error_column;
                snprintf(
                        program->error,
                        sizeof(program->error),
                        "%s",
                        lexer.error[0]
                                ? lexer.error
                                : "failed to tokenize Lua source"
                );
                return ABNORMAL;
        }

        return book_parser_parse(&lexer, program);
}

void book_program_free(BOOK_PROGRAM * program) {
        if (!program) return;

        book_lexer_free(&program->lexer);

        if (program->nodes) {
                memset(
                        program->nodes,
                        0x00,
                        (c_size_t)(program->node_capacity + 1U)
                                * sizeof(*program->nodes)
                );
                free(program->nodes);
        }

        if (program->lists) {
                memset(
                        program->lists,
                        0x00,
                        (c_size_t)(program->list_capacity + 1U)
                                * sizeof(*program->lists)
                );
                free(program->lists);
        }

        memset(program, 0x00, sizeof(*program));
        return;
}

const BOOK_AST_NODE * book_program_node(
        const BOOK_PROGRAM * program,
        uint32_t node_id
) {
        if (
                !program
                || node_id == 0
                || node_id > program->node_count
                || !program->nodes
        ) {
                return NULL;
        }

        return &program->nodes[node_id];
}

const BOOK_AST_LIST_ENTRY * book_program_list_entry(
        const BOOK_PROGRAM * program,
        uint32_t list_id
) {
        if (
                !program
                || list_id == 0
                || list_id > program->list_count
                || !program->lists
        ) {
                return NULL;
        }

        return &program->lists[list_id];
}

const char * book_ast_kind_name(book_ast_kind_t kind) {
        switch (kind) {
                case BOOK_AST_NONE:             return "NONE";
                case BOOK_AST_NIL:              return "NIL";
                case BOOK_AST_BOOLEAN:          return "BOOLEAN";
                case BOOK_AST_NUMBER:           return "NUMBER";
                case BOOK_AST_STRING:           return "STRING";
                case BOOK_AST_VARIABLE:         return "VARIABLE";
                case BOOK_AST_VARARG:           return "VARARG";
                case BOOK_AST_TABLE:            return "TABLE";
                case BOOK_AST_TABLE_FIELD:      return "TABLE_FIELD";
                case BOOK_AST_FUNCTION:         return "FUNCTION";
                case BOOK_AST_UNARY:            return "UNARY";
                case BOOK_AST_BINARY:           return "BINARY";
                case BOOK_AST_FIELD:            return "FIELD";
                case BOOK_AST_INDEX:            return "INDEX";
                case BOOK_AST_CALL:             return "CALL";
                case BOOK_AST_EXPR_STATEMENT:   return "EXPR_STATEMENT";
                case BOOK_AST_ASSIGN:           return "ASSIGN";
                case BOOK_AST_LOCAL_ASSIGN:     return "LOCAL_ASSIGN";
                case BOOK_AST_IF:               return "IF";
                case BOOK_AST_IF_CLAUSE:        return "IF_CLAUSE";
                case BOOK_AST_WHILE:            return "WHILE";
                case BOOK_AST_FOR_NUMERIC:      return "FOR_NUMERIC";
                case BOOK_AST_FOR_GENERIC:      return "FOR_GENERIC";
                case BOOK_AST_BREAK:            return "BREAK";
                case BOOK_AST_RETURN:           return "RETURN";
                case BOOK_AST_FUNCTION_DECL:    return "FUNCTION_DECL";
                case BOOK_AST_LOCAL_FUNCTION:   return "LOCAL_FUNCTION";
                default:                        return "UNKNOWN";
        }
}
