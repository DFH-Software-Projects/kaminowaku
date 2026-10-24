// Copyright 2026 Jamison A. Drapeau
#include "book_args.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

static int book_args_error(char * error, size_t size, const char * message) {
        if (error && size > 0) (void)snprintf(error, size, "%s", message);
        return -1;
}

// @@ Read one shell-like word without invoking a shell or modifying global
// command tokenization. Single/double quotes group whitespace. Within quotes,
// backslash escapes only the matching quote or another backslash.
static int book_args_word(
        const char ** position,
        char * word,
        size_t capacity,
        char * error,
        size_t error_size
) {
        const char * cursor;
        size_t length = 0;
        char quote = 0;

        if (!position || !*position || !word || capacity == 0) {
                return book_args_error(error, error_size, "Invalid Book argument parser state.");
        }

        cursor = *position;
        while (*cursor && isspace((unsigned char)*cursor)) cursor++;
        if (!*cursor) {
                word[0] = '\0';
                *position = cursor;
                return 0;
        }

        while (*cursor) {
                unsigned char ch = (unsigned char)*cursor;
                if (!quote && isspace(ch)) break;

                if (ch == '"' || ch == '\'') {
                        if (!quote) {
                                quote = (char)ch;
                                cursor++;
                                continue;
                        }
                        if (quote == (char)ch) {
                                quote = 0;
                                cursor++;
                                continue;
                        }
                }

                if (quote && ch == '\\'
                        && (cursor[1] == quote || cursor[1] == '\\')) {
                        cursor++;
                        ch = (unsigned char)*cursor;
                }

                if (length + 1 >= capacity) {
                        return book_args_error(error, error_size, "Book argument exceeds the maximum length.");
                }
                word[length++] = (char)ch;
                cursor++;
        }

        if (quote) {
                return book_args_error(error, error_size, "Unterminated quote in Book arguments.");
        }

        word[length] = '\0';
        *position = cursor;
        return 1;
}

static int book_args_valid_name(const char * name) {
        size_t length;
        if (!name || !name[0]) return 0;
        length = strlen(name);
        if (length >= BOOK_ARG_NAME_BLOCK) return 0;

        for (size_t i = 0; i < length; i++) {
                unsigned char ch = (unsigned char)name[i];
                if (i == 0) {
                        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')
                                || ch == '_')) return 0;
                } else if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')
                                || (ch >= '0' && ch <= '9') || ch == '_' || ch == '-')) {
                        return 0;
                }
        }
        return 1;
}

int book_args_parse(
        const char * command,
        const char * expected_book_name,
        BOOK_ARGUMENTS * arguments,
        char * error,
        size_t error_size
) {
        const char * cursor = command;
        char token[BOOK_ARG_NAME_BLOCK + BOOK_ARG_VALUE_BLOCK + 8];
        int status;

        if (!command || !expected_book_name || !arguments) {
                return book_args_error(error, error_size, "Invalid Book invocation.");
        }

        memset(arguments, 0, sizeof(*arguments));
        if (error && error_size) error[0] = '\0';

        status = book_args_word(&cursor, token, sizeof(token), error, error_size);
        if (status != 1 || strcmp(token, "book") != 0) {
                return book_args_error(error, error_size, "Expected 'book <name>'.");
        }

        status = book_args_word(&cursor, token, sizeof(token), error, error_size);
        if (status != 1 || strcmp(token, expected_book_name) != 0) {
                return book_args_error(error, error_size, "Invalid Book name in command.");
        }

        for (;;) {
                char value[BOOK_ARG_VALUE_BLOCK];
                const char * name;
                char * separator;

                status = book_args_word(&cursor, token, sizeof(token), error, error_size);
                if (status < 0) return -1;
                if (status == 0) break;

                if (token[0] != '-' || token[1] != '-') {
                        return book_args_error(error, error_size, "Expected a named Book parameter (--name value).");
                }

                name = token + 2;
                separator = strchr(token + 2, '=');
                if (separator) *separator = '\0';
                if (!book_args_valid_name(name)) {
                        return book_args_error(error, error_size, "Invalid Book parameter name.");
                }

                if (arguments->count >= BOOK_ARGS_LIMIT) {
                        return book_args_error(error, error_size, "Too many Book parameters (limit: 16).");
                }

                for (size_t i = 0; i < arguments->count; i++) {
                        if (strcmp(arguments->items[i].name, name) == 0) {
                                return book_args_error(error, error_size, "Duplicate Book parameter.");
                        }
                }

                if (separator) {
                        const char * inline_value = separator + 1;
                        if (strlen(inline_value) >= sizeof(value)) {
                                return book_args_error(error, error_size, "Book parameter value is too long.");
                        }
                        (void)snprintf(value, sizeof(value), "%s", inline_value);
                } else {
                        status = book_args_word(&cursor, value, sizeof(value), error, error_size);
                        if (status < 0) return -1;
                        if (status == 0 || strncmp(value, "--", 2) == 0) {
                                return book_args_error(error, error_size, "Missing Book parameter value.");
                        }
                }

                BOOK_ARGUMENT * entry = &arguments->items[arguments->count++];
                (void)snprintf(entry->name, sizeof(entry->name), "%s", name);
                (void)snprintf(entry->value, sizeof(entry->value), "%s", value);
        }

        return 0;
}
