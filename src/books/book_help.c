// Copyright 2026 Jamison A. Drapeau
#include "book_help.h"
#include "book_parser.h"
#include <string.h>

// @@ The help path is static AST inspection, never Lua evaluation.
// Only literal metadata embedded in a top-level book { ... } is admitted.
static int book_help_identifier(
        const BOOK_PROGRAM * program,
        c_size_t index,
        const char * expected
) {
        const BOOK_TOKEN * token;
        size_t length;
        if (!program || !expected || index >= program->lexer.count) return -1;
        token = &program->lexer.tokens[index];
        length = strlen(expected);
        if (token->kind != BOOK_TOKEN_IDENTIFIER || token->length != length
                || token->offset > program->lexer.source_length
                || length > program->lexer.source_length - token->offset) return -1;
        return memcmp(program->lexer.source + token->offset, expected, length) == 0
                ? 0 : -1;
}

static int book_help_string(
        const BOOK_PROGRAM * program,
        uint32_t node_id,
        char * destination,
        size_t capacity
) {
        const BOOK_AST_NODE * node = book_program_node(program, node_id);
        const BOOK_TOKEN * token;
        c_size_t length = 0;

        if (!node || node->kind != BOOK_AST_STRING || !destination
                || capacity == 0 || node->token >= program->lexer.count) return -1;
        token = &program->lexer.tokens[node->token];
        if (book_lexer_decode_string(
                    &program->lexer, token, (unsigned char*)destination,
                    capacity, &length
            ) != NORMAL) return -1;

        // Untrusted Book metadata is displayed in the TUI: no embedded
        // control/ANSI bytes, newline, NUL, or unbounded strings.
        for (c_size_t i = 0; i < length; i++) {
                unsigned char ch = (unsigned char)destination[i];
                if (ch < 0x20 || ch > 0x7e) return -1;
        }
        destination[length] = '\0';
        return 0;
}

static int book_help_field_key(
        const BOOK_PROGRAM * program,
        const BOOK_AST_NODE * field,
        char * name,
        size_t capacity
) {
        const BOOK_TOKEN * token;
        if (!field || !name || capacity == 0) return -1;

        if (field->flags == BOOK_TABLE_FIELD_INDEXED) {
                return book_help_string(program, field->a, name, capacity);
        }
        if (field->flags != BOOK_TABLE_FIELD_NAMED
                || field->token >= program->lexer.count) return -1;
        token = &program->lexer.tokens[field->token];
        if (token->kind != BOOK_TOKEN_IDENTIFIER || token->length >= capacity
                || token->offset > program->lexer.source_length
                || token->length > program->lexer.source_length - token->offset) return -1;
        memcpy(name, program->lexer.source + token->offset, token->length);
        name[token->length] = '\0';
        return 0;
}

static const BOOK_AST_NODE * book_help_named_field(
        const BOOK_PROGRAM * program,
        const BOOK_AST_NODE * table,
        const char * name
) {
        uint32_t position;
        if (!table || table->kind != BOOK_AST_TABLE) return NULL;
        position = table->list1;
        while (position) {
                const BOOK_AST_LIST_ENTRY * entry =
                        book_program_list_entry(program, position);
                const BOOK_AST_NODE * field;
                char key[BOOK_HELP_NAME_BLOCK];
                if (!entry) return NULL;
                field = book_program_node(program, entry->node);
                if (field && field->kind == BOOK_AST_TABLE_FIELD
                        && book_help_field_key(program, field, key, sizeof(key)) == 0
                        && strcmp(key, name) == 0) return field;
                position = entry->next;
        }
        return NULL;
}

static int book_help_option_name(const char * name) {
        size_t i;
        if (!name || !name[0]) return -1;
        for (i = 0; name[i]; i++) {
                unsigned char ch = (unsigned char)name[i];
                if (i == 0) {
                        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')
                                || ch == '_')) return -1;
                } else if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')
                                || (ch >= '0' && ch <= '9') || ch == '_' || ch == '-')) {
                        return -1;
                }
        }
        return strcmp(name, "help") == 0 ? -1 : 0;
}

static int book_help_options(
        const BOOK_PROGRAM * program,
        const BOOK_AST_NODE * table,
        BOOK_HELP * help
) {
        uint32_t position;
        if (!table || table->kind != BOOK_AST_TABLE) return -1;
        position = table->list1;
        while (position) {
                const BOOK_AST_LIST_ENTRY * entry =
                        book_program_list_entry(program, position);
                const BOOK_AST_NODE * field;
                BOOK_HELP_OPTION * option;
                if (!entry || help->option_count >= BOOK_HELP_OPTION_LIMIT) return -1;
                field = book_program_node(program, entry->node);
                if (!field || field->kind != BOOK_AST_TABLE_FIELD) return -1;
                option = &help->options[help->option_count];
                if (book_help_field_key(program, field, option->name, sizeof(option->name)) != 0
                        || book_help_option_name(option->name) != 0
                        || book_help_string(program, field->b, option->description,
                                sizeof(option->description)) != 0) return -1;
                for (size_t i = 0; i < help->option_count; i++) {
                        if (strcmp(help->options[i].name, option->name) == 0) return -1;
                }
                help->option_count++;
                position = entry->next;
        }
        return 0;
}

static int book_help_extract(
        const BOOK_PROGRAM * program,
        const char * expected_name,
        BOOK_HELP * help
) {
        uint32_t statement_id = program->root;
        while (statement_id) {
                const BOOK_AST_NODE * statement =
                        book_program_node(program, statement_id);
                const BOOK_AST_NODE * call;
                const BOOK_AST_NODE * callee;
                const BOOK_AST_LIST_ENTRY * argument;
                const BOOK_AST_NODE * declaration;
                const BOOK_AST_NODE * name_field, * description_field, * options_field;
                char declared_name[BOOK_HELP_NAME_BLOCK];
                if (!statement) return -1;
                statement_id = statement->next;
                if (statement->kind != BOOK_AST_EXPR_STATEMENT) continue;
                call = book_program_node(program, statement->a);
                if (!call || call->kind != BOOK_AST_CALL) continue;
                callee = book_program_node(program, call->a);
                if (!callee || callee->kind != BOOK_AST_VARIABLE
                        || book_help_identifier(program, callee->token, "book") != 0) continue;
                argument = book_program_list_entry(program, call->list1);
                if (!argument || argument->next) return -1;
                declaration = book_program_node(program, argument->node);
                if (!declaration || declaration->kind != BOOK_AST_TABLE) return -1;
                name_field = book_help_named_field(program, declaration, "name");
                if (!name_field || book_help_string(program, name_field->b,
                                declared_name, sizeof(declared_name)) != 0
                        || strcmp(declared_name, expected_name) != 0) return -1;
                description_field = book_help_named_field(program, declaration, "description");
                if (description_field && book_help_string(program, description_field->b,
                                help->description, sizeof(help->description)) != 0) return -1;
                options_field = book_help_named_field(program, declaration, "options");
                if (options_field && book_help_options(program,
                            book_program_node(program, options_field->b), help) != 0) return -1;
                return 0;
        }
        return -1;
}

int book_help_read(
        const unsigned char * path,
        const char * expected_name,
        BOOK_HELP * help
) {
        BOOK_PROGRAM program;
        int result;
        if (!path || !expected_name || !help) return -1;
        memset(help, 0, sizeof(*help));
        memset(&program, 0, sizeof(program));
        if (book_parser_parse_file(path, &program) != NORMAL) {
                book_program_free(&program);
                return -1;
        }
        result = book_help_extract(&program, expected_name, help);
        book_program_free(&program);
        if (result != 0) memset(help, 0, sizeof(*help));
        return result;
}
