// Copyright 2026 Jamison A. Drapeau
#include "book_lexer.h"
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct BOOK_LEXER_STATE {
        BOOK_LEXER_RESULT * result;
        c_size_t            offset;
        unsigned int        line;
        unsigned int        column;
} BOOK_LEXER_STATE;

typedef struct BOOK_LEXER_KEYWORD {
        const char *        text;
        c_size_t            length;
        book_token_kind_t   kind;
} BOOK_LEXER_KEYWORD;

static const BOOK_LEXER_KEYWORD BOOK_LEXER_KEYWORDS[] = {
        {"and",      3, BOOK_TOKEN_AND},
        {"break",    5, BOOK_TOKEN_BREAK},
        {"do",       2, BOOK_TOKEN_DO},
        {"else",     4, BOOK_TOKEN_ELSE},
        {"elseif",   6, BOOK_TOKEN_ELSEIF},
        {"end",      3, BOOK_TOKEN_END},
        {"false",    5, BOOK_TOKEN_FALSE},
        {"for",      3, BOOK_TOKEN_FOR},
        {"function", 8, BOOK_TOKEN_FUNCTION},
        {"if",       2, BOOK_TOKEN_IF},
        {"in",       2, BOOK_TOKEN_IN},
        {"local",    5, BOOK_TOKEN_LOCAL},
        {"nil",      3, BOOK_TOKEN_NIL},
        {"not",      3, BOOK_TOKEN_NOT},
        {"or",       2, BOOK_TOKEN_OR},
        {"return",   6, BOOK_TOKEN_RETURN},
        {"then",     4, BOOK_TOKEN_THEN},
        {"true",     4, BOOK_TOKEN_TRUE},
        {"while",    5, BOOK_TOKEN_WHILE}
};

// @@ Lua keywords outside BOOK_LANGUAGE_V1.md remain reserved and fail closed.
// Treating them as identifiers would create a language that only looks like Lua.
static const char * BOOK_LEXER_UNSUPPORTED_KEYWORDS[] = {
        "goto",
        "repeat",
        "until"
};

static int book_lexer_fail(
        BOOK_LEXER_STATE * state,
        unsigned int line,
        unsigned int column,
        const char * format,
        ...
) {
        va_list arguments;

        if (!state || !state->result || !format) return ABNORMAL;

        if (state->result->status != ABNORMAL) {
                state->result->status = ABNORMAL;
                state->result->error_line = line;
                state->result->error_column = column;

                va_start(arguments, format);
                (void)vsnprintf(
                        state->result->error,
                        sizeof(state->result->error),
                        format,
                        arguments
                );
                va_end(arguments);
        }

        return ABNORMAL;
}

static int book_lexer_peek(
        const BOOK_LEXER_STATE * state,
        c_size_t ahead
) {
        c_size_t offset;

        if (!state || !state->result || !state->result->source) return -1;
        if (ahead > state->result->source_length) return -1;
        if (state->offset > state->result->source_length - ahead) return -1;

        offset = state->offset + ahead;
        if (offset >= state->result->source_length) return -1;
        return (int)state->result->source[offset];
}

// @@ Advance one logical source character. CRLF counts as one newline while
// token offsets still retain both source bytes.
static int book_lexer_advance(BOOK_LEXER_STATE * state) {
        int value;

        if (!state || !state->result) return -1;
        value = book_lexer_peek(state, 0);
        if (value < 0) return -1;

        state->offset++;

        if (value == '\r') {
                if (book_lexer_peek(state, 0) == '\n') {
                        state->offset++;
                }
                state->line++;
                state->column = 1;
                return value;
        }

        if (value == '\n') {
                state->line++;
                state->column = 1;
                return value;
        }

        state->column++;
        return value;
}

static int book_lexer_is_space(int value) {
        return (
                value == ' '
                || value == '\t'
                || value == '\v'
                || value == '\f'
                || value == '\r'
                || value == '\n'
        ) ? ISTRUE : ISFALSE;
}

static int book_lexer_is_alpha(int value) {
        return (
                (value >= 'A' && value <= 'Z')
                || (value >= 'a' && value <= 'z')
                || value == '_'
        ) ? ISTRUE : ISFALSE;
}

static int book_lexer_is_digit(int value) {
        return value >= '0' && value <= '9' ? ISTRUE : ISFALSE;
}

static int book_lexer_is_hex(int value) {
        return (
                (value >= '0' && value <= '9')
                || (value >= 'a' && value <= 'f')
                || (value >= 'A' && value <= 'F')
        ) ? ISTRUE : ISFALSE;
}

static int book_lexer_is_alnum(int value) {
        return (
                book_lexer_is_alpha(value) == ISTRUE
                || book_lexer_is_digit(value) == ISTRUE
        ) ? ISTRUE : ISFALSE;
}

static int book_lexer_long_bracket_open(
        const BOOK_LEXER_STATE * state,
        c_size_t offset
) {
        c_size_t cursor;

        if (!state || !state->result || !state->result->source) return ISFALSE;
        if (offset >= state->result->source_length) return ISFALSE;
        if (state->result->source[offset] != '[') return ISFALSE;

        cursor = offset + 1;
        while (
                cursor < state->result->source_length
                && state->result->source[cursor] == '='
        ) {
                cursor++;
        }

        return (
                cursor < state->result->source_length
                && state->result->source[cursor] == '['
        ) ? ISTRUE : ISFALSE;
}

static int book_lexer_reserve(BOOK_LEXER_STATE * state) {
        BOOK_TOKEN * expanded;
        c_size_t next;

        if (!state || !state->result) return ABNORMAL;
        if (state->result->count < state->result->capacity) return NORMAL;

        if (state->result->count >= BOOK_LEXER_TOKEN_LIMIT) {
                return book_lexer_fail(
                        state,
                        state->line,
                        state->column,
                        "token limit exceeded"
                );
        }

        next = state->result->capacity
                ? state->result->capacity * 2U
                : 128U;

        if (next > BOOK_LEXER_TOKEN_LIMIT) {
                next = BOOK_LEXER_TOKEN_LIMIT;
        }

        expanded = realloc(
                state->result->tokens,
                next * sizeof(*expanded)
        );
        if (!expanded) {
                return book_lexer_fail(
                        state,
                        state->line,
                        state->column,
                        "failed to allocate token storage"
                );
        }

        memset(
                expanded + state->result->capacity,
                0x00,
                (next - state->result->capacity) * sizeof(*expanded)
        );

        state->result->tokens = expanded;
        state->result->capacity = next;
        return NORMAL;
}

static int book_lexer_emit(
        BOOK_LEXER_STATE * state,
        book_token_kind_t kind,
        c_size_t offset,
        c_size_t length,
        unsigned int line,
        unsigned int column
) {
        BOOK_TOKEN * token;

        if (!state || !state->result) return ABNORMAL;
        if (state->result->count >= BOOK_LEXER_TOKEN_LIMIT) {
                return book_lexer_fail(
                        state,
                        line,
                        column,
                        "token limit exceeded"
                );
        }

        if (book_lexer_reserve(state) != NORMAL) return ABNORMAL;

        token = &state->result->tokens[state->result->count++];
        memset(token, 0x00, sizeof(*token));
        token->kind = kind;
        token->offset = offset;
        token->length = length;
        token->line = line;
        token->column = column;
        return NORMAL;
}

static book_token_kind_t book_lexer_keyword(
        const unsigned char * text,
        c_size_t length
) {
        c_size_t count = sizeof(BOOK_LEXER_KEYWORDS) / sizeof(BOOK_LEXER_KEYWORDS[0]);

        if (!text || length == 0) return BOOK_TOKEN_IDENTIFIER;

        for (c_size_t i = 0; i < count; i++) {
                if (
                        BOOK_LEXER_KEYWORDS[i].length == length
                        && memcmp(text, BOOK_LEXER_KEYWORDS[i].text, length) == MATCH
                ) {
                        return BOOK_LEXER_KEYWORDS[i].kind;
                }
        }

        return BOOK_TOKEN_IDENTIFIER;
}

static int book_lexer_unsupported_keyword(
        const unsigned char * text,
        c_size_t length
) {
        c_size_t count = sizeof(BOOK_LEXER_UNSUPPORTED_KEYWORDS)
                / sizeof(BOOK_LEXER_UNSUPPORTED_KEYWORDS[0]);

        if (!text || length == 0) return ISFALSE;

        for (c_size_t i = 0; i < count; i++) {
                c_size_t keyword_length = (c_size_t)strlen(
                        BOOK_LEXER_UNSUPPORTED_KEYWORDS[i]
                );

                if (
                        keyword_length == length
                        && memcmp(
                                text,
                                BOOK_LEXER_UNSUPPORTED_KEYWORDS[i],
                                length
                        ) == MATCH
                ) {
                        return ISTRUE;
                }
        }

        return ISFALSE;
}

// @@ Skip whitespace and Lua line comments. Long-bracket comments are outside
// the v1 contract and fail explicitly instead of being misread as line comments.
static int book_lexer_skip_ignored(BOOK_LEXER_STATE * state) {
        for (;;) {
                int value;

                if (!state) return ABNORMAL;
                value = book_lexer_peek(state, 0);

                while (
                        value >= 0
                        && book_lexer_is_space(value) == ISTRUE
                ) {
                        (void)book_lexer_advance(state);
                        value = book_lexer_peek(state, 0);
                }

                if (
                        value == '-'
                        && book_lexer_peek(state, 1) == '-'
                ) {
                        unsigned int line = state->line;
                        unsigned int column = state->column;

                        if (
                                state->offset + 2U < state->result->source_length
                                && book_lexer_long_bracket_open(
                                        state,
                                        state->offset + 2U
                                ) == ISTRUE
                        ) {
                                return book_lexer_fail(
                                        state,
                                        line,
                                        column,
                                        "long-bracket comments are not supported"
                                );
                        }

                        (void)book_lexer_advance(state);
                        (void)book_lexer_advance(state);

                        value = book_lexer_peek(state, 0);
                        while (
                                value >= 0
                                && value != '\r'
                                && value != '\n'
                        ) {
                                (void)book_lexer_advance(state);
                                value = book_lexer_peek(state, 0);
                        }
                        continue;
                }

                return NORMAL;
        }
}

static int book_lexer_identifier(BOOK_LEXER_STATE * state) {
        c_size_t start;
        c_size_t length;
        unsigned int line;
        unsigned int column;
        book_token_kind_t kind;

        if (!state || !state->result) return ABNORMAL;

        start = state->offset;
        line = state->line;
        column = state->column;

        while (
                book_lexer_is_alnum(book_lexer_peek(state, 0)) == ISTRUE
        ) {
                (void)book_lexer_advance(state);
        }

        length = state->offset - start;

        if (
                book_lexer_unsupported_keyword(
                        state->result->source + start,
                        length
                ) == ISTRUE
        ) {
                return book_lexer_fail(
                        state,
                        line,
                        column,
                        "Lua keyword is outside the Kaminowaku v1 subset"
                );
        }

        kind = book_lexer_keyword(
                state->result->source + start,
                length
        );

        return book_lexer_emit(
                state,
                kind,
                start,
                length,
                line,
                column
        );
}

static int book_lexer_number(BOOK_LEXER_STATE * state) {
        c_size_t start;
        unsigned int line;
        unsigned int column;
        int value;

        if (!state || !state->result) return ABNORMAL;

        start = state->offset;
        line = state->line;
        column = state->column;

        // Hexadecimal integer literal.
        if (
                book_lexer_peek(state, 0) == '0'
                && (
                        book_lexer_peek(state, 1) == 'x'
                        || book_lexer_peek(state, 1) == 'X'
                )
        ) {
                (void)book_lexer_advance(state);
                (void)book_lexer_advance(state);

                if (book_lexer_is_hex(book_lexer_peek(state, 0)) != ISTRUE) {
                        return book_lexer_fail(
                                state,
                                line,
                                column,
                                "malformed hexadecimal number"
                        );
                }

                while (
                        book_lexer_is_hex(book_lexer_peek(state, 0)) == ISTRUE
                ) {
                        (void)book_lexer_advance(state);
                }

                if (
                        book_lexer_is_alpha(book_lexer_peek(state, 0)) == ISTRUE
                ) {
                        return book_lexer_fail(
                                state,
                                line,
                                column,
                                "malformed hexadecimal number"
                        );
                }

                return book_lexer_emit(
                        state,
                        BOOK_TOKEN_NUMBER,
                        start,
                        state->offset - start,
                        line,
                        column
                );
        }

        // Decimal integer/floating literal, including .5.
        if (book_lexer_peek(state, 0) == '.') {
                (void)book_lexer_advance(state);
                while (
                        book_lexer_is_digit(book_lexer_peek(state, 0)) == ISTRUE
                ) {
                        (void)book_lexer_advance(state);
                }
        } else {
                while (
                        book_lexer_is_digit(book_lexer_peek(state, 0)) == ISTRUE
                ) {
                        (void)book_lexer_advance(state);
                }

                if (
                        book_lexer_peek(state, 0) == '.'
                        && book_lexer_peek(state, 1) != '.'
                ) {
                        (void)book_lexer_advance(state);
                        while (
                                book_lexer_is_digit(book_lexer_peek(state, 0)) == ISTRUE
                        ) {
                                (void)book_lexer_advance(state);
                        }
                }
        }

        value = book_lexer_peek(state, 0);
        if (value == 'e' || value == 'E') {
                (void)book_lexer_advance(state);

                value = book_lexer_peek(state, 0);
                if (value == '+' || value == '-') {
                        (void)book_lexer_advance(state);
                }

                if (
                        book_lexer_is_digit(book_lexer_peek(state, 0)) != ISTRUE
                ) {
                        return book_lexer_fail(
                                state,
                                line,
                                column,
                                "malformed numeric exponent"
                        );
                }

                while (
                        book_lexer_is_digit(book_lexer_peek(state, 0)) == ISTRUE
                ) {
                        (void)book_lexer_advance(state);
                }
        }

        if (
                book_lexer_is_alpha(book_lexer_peek(state, 0)) == ISTRUE
        ) {
                return book_lexer_fail(
                        state,
                        line,
                        column,
                        "malformed number"
                );
        }

        return book_lexer_emit(
                state,
                BOOK_TOKEN_NUMBER,
                start,
                state->offset - start,
                line,
                column
        );
}

static int book_lexer_string(BOOK_LEXER_STATE * state) {
        int quote;
        c_size_t content_start;
        unsigned int line;
        unsigned int column;

        if (!state || !state->result) return ABNORMAL;

        quote = book_lexer_peek(state, 0);
        line = state->line;
        column = state->column;

        (void)book_lexer_advance(state);
        content_start = state->offset;

        for (;;) {
                int value = book_lexer_peek(state, 0);

                if (value < 0) {
                        return book_lexer_fail(
                                state,
                                line,
                                column,
                                "unterminated string"
                        );
                }

                if (value == 0x00) {
                        return book_lexer_fail(
                                state,
                                state->line,
                                state->column,
                                "NUL byte is not permitted directly in Lua source"
                        );
                }

                if (value == quote) {
                        c_size_t content_length = state->offset - content_start;

                        (void)book_lexer_advance(state);
                        return book_lexer_emit(
                                state,
                                BOOK_TOKEN_STRING,
                                content_start,
                                content_length,
                                line,
                                column
                        );
                }

                if (value == '\r' || value == '\n') {
                        return book_lexer_fail(
                                state,
                                state->line,
                                state->column,
                                "unescaped newline in string"
                        );
                }

                if (value != '\\') {
                        (void)book_lexer_advance(state);
                        continue;
                }

                (void)book_lexer_advance(state);
                value = book_lexer_peek(state, 0);

                if (value < 0) {
                        return book_lexer_fail(
                                state,
                                line,
                                column,
                                "unterminated string escape"
                        );
                }

                if (
                        value == 'a'
                        || value == 'b'
                        || value == 'f'
                        || value == 'n'
                        || value == 'r'
                        || value == 't'
                        || value == 'v'
                        || value == '\\'
                        || value == '"'
                        || value == '\''
                ) {
                        (void)book_lexer_advance(state);
                        continue;
                }

                if (value == 'x') {
                        (void)book_lexer_advance(state);

                        if (
                                book_lexer_is_hex(book_lexer_peek(state, 0)) != ISTRUE
                                || book_lexer_is_hex(book_lexer_peek(state, 1)) != ISTRUE
                        ) {
                                return book_lexer_fail(
                                        state,
                                        state->line,
                                        state->column,
                                        "hex string escape requires exactly two hex digits"
                                );
                        }

                        (void)book_lexer_advance(state);
                        (void)book_lexer_advance(state);
                        continue;
                }

                if (book_lexer_is_digit(value) == ISTRUE) {
                        unsigned int decimal = 0;
                        unsigned int digits = 0;

                        while (
                                digits < 3U
                                && book_lexer_is_digit(book_lexer_peek(state, 0)) == ISTRUE
                        ) {
                                decimal = (decimal * 10U)
                                        + (unsigned int)(book_lexer_peek(state, 0) - '0');
                                digits++;
                                (void)book_lexer_advance(state);
                        }

                        if (decimal > 255U) {
                                return book_lexer_fail(
                                        state,
                                        state->line,
                                        state->column,
                                        "decimal string escape exceeds one byte"
                                );
                        }
                        continue;
                }

                if (value == 'z') {
                        (void)book_lexer_advance(state);
                        while (
                                book_lexer_is_space(book_lexer_peek(state, 0)) == ISTRUE
                        ) {
                                (void)book_lexer_advance(state);
                        }
                        continue;
                }

                if (value == '\r' || value == '\n') {
                        (void)book_lexer_advance(state);
                        continue;
                }

                return book_lexer_fail(
                        state,
                        state->line,
                        state->column,
                        "unsupported string escape"
                );
        }
}

static int book_lexer_single_token(
        BOOK_LEXER_STATE * state,
        book_token_kind_t kind
) {
        c_size_t start;
        unsigned int line;
        unsigned int column;

        if (!state) return ABNORMAL;

        start = state->offset;
        line = state->line;
        column = state->column;
        (void)book_lexer_advance(state);

        return book_lexer_emit(
                state,
                kind,
                start,
                1,
                line,
                column
        );
}

static int book_lexer_operator(BOOK_LEXER_STATE * state) {
        c_size_t start;
        unsigned int line;
        unsigned int column;
        int value;
        int next;

        if (!state) return ABNORMAL;

        start = state->offset;
        line = state->line;
        column = state->column;
        value = book_lexer_peek(state, 0);
        next = book_lexer_peek(state, 1);

        switch (value) {
                case '+':
                        return book_lexer_single_token(state, BOOK_TOKEN_PLUS);
                case '-':
                        return book_lexer_single_token(state, BOOK_TOKEN_MINUS);
                case '*':
                        return book_lexer_single_token(state, BOOK_TOKEN_STAR);
                case '%':
                        return book_lexer_single_token(state, BOOK_TOKEN_PERCENT);
                case '#':
                        return book_lexer_single_token(state, BOOK_TOKEN_LENGTH);
                case '&':
                        return book_lexer_single_token(state, BOOK_TOKEN_BIT_AND);
                case '|':
                        return book_lexer_single_token(state, BOOK_TOKEN_BIT_OR);
                case '(':
                        return book_lexer_single_token(state, BOOK_TOKEN_LEFT_PAREN);
                case ')':
                        return book_lexer_single_token(state, BOOK_TOKEN_RIGHT_PAREN);
                case '{':
                        return book_lexer_single_token(state, BOOK_TOKEN_LEFT_BRACE);
                case '}':
                        return book_lexer_single_token(state, BOOK_TOKEN_RIGHT_BRACE);
                case ']':
                        return book_lexer_single_token(state, BOOK_TOKEN_RIGHT_BRACKET);
                case ',':
                        return book_lexer_single_token(state, BOOK_TOKEN_COMMA);
                case ':':
                        return book_lexer_single_token(state, BOOK_TOKEN_COLON);
                case ';':
                        return book_lexer_single_token(state, BOOK_TOKEN_SEMICOLON);

                case '[':
                        if (
                                book_lexer_long_bracket_open(
                                        state,
                                        state->offset
                                ) == ISTRUE
                        ) {
                                return book_lexer_fail(
                                        state,
                                        line,
                                        column,
                                        "long-bracket strings are not supported"
                                );
                        }
                        return book_lexer_single_token(
                                state,
                                BOOK_TOKEN_LEFT_BRACKET
                        );

                case '/':
                        (void)book_lexer_advance(state);
                        if (next == '/') {
                                (void)book_lexer_advance(state);
                                return book_lexer_emit(
                                        state,
                                        BOOK_TOKEN_FLOOR_DIV,
                                        start,
                                        2,
                                        line,
                                        column
                                );
                        }
                        return book_lexer_emit(
                                state,
                                BOOK_TOKEN_SLASH,
                                start,
                                1,
                                line,
                                column
                        );

                case '.':
                        if (book_lexer_is_digit(next) == ISTRUE) {
                                return book_lexer_number(state);
                        }

                        (void)book_lexer_advance(state);

                        if (
                                next == '.'
                                && book_lexer_peek(state, 1) == '.'
                        ) {
                                (void)book_lexer_advance(state);
                                (void)book_lexer_advance(state);
                                return book_lexer_emit(
                                        state,
                                        BOOK_TOKEN_ELLIPSIS,
                                        start,
                                        3,
                                        line,
                                        column
                                );
                        }

                        if (next == '.') {
                                (void)book_lexer_advance(state);
                                return book_lexer_emit(
                                        state,
                                        BOOK_TOKEN_CONCAT,
                                        start,
                                        2,
                                        line,
                                        column
                                );
                        }

                        return book_lexer_emit(
                                state,
                                BOOK_TOKEN_DOT,
                                start,
                                1,
                                line,
                                column
                        );

                case '~':
                        (void)book_lexer_advance(state);
                        if (next == '=') {
                                (void)book_lexer_advance(state);
                                return book_lexer_emit(
                                        state,
                                        BOOK_TOKEN_NOT_EQUAL,
                                        start,
                                        2,
                                        line,
                                        column
                                );
                        }
                        return book_lexer_emit(
                                state,
                                BOOK_TOKEN_TILDE,
                                start,
                                1,
                                line,
                                column
                        );

                case '=':
                        (void)book_lexer_advance(state);
                        if (next == '=') {
                                (void)book_lexer_advance(state);
                                return book_lexer_emit(
                                        state,
                                        BOOK_TOKEN_EQUAL_EQUAL,
                                        start,
                                        2,
                                        line,
                                        column
                                );
                        }
                        return book_lexer_emit(
                                state,
                                BOOK_TOKEN_EQUAL,
                                start,
                                1,
                                line,
                                column
                        );

                case '<':
                        (void)book_lexer_advance(state);
                        if (next == '=') {
                                (void)book_lexer_advance(state);
                                return book_lexer_emit(
                                        state,
                                        BOOK_TOKEN_LESS_EQUAL,
                                        start,
                                        2,
                                        line,
                                        column
                                );
                        }
                        if (next == '<') {
                                (void)book_lexer_advance(state);
                                return book_lexer_emit(
                                        state,
                                        BOOK_TOKEN_SHIFT_LEFT,
                                        start,
                                        2,
                                        line,
                                        column
                                );
                        }
                        return book_lexer_emit(
                                state,
                                BOOK_TOKEN_LESS,
                                start,
                                1,
                                line,
                                column
                        );

                case '>':
                        (void)book_lexer_advance(state);
                        if (next == '=') {
                                (void)book_lexer_advance(state);
                                return book_lexer_emit(
                                        state,
                                        BOOK_TOKEN_GREATER_EQUAL,
                                        start,
                                        2,
                                        line,
                                        column
                                );
                        }
                        if (next == '>') {
                                (void)book_lexer_advance(state);
                                return book_lexer_emit(
                                        state,
                                        BOOK_TOKEN_SHIFT_RIGHT,
                                        start,
                                        2,
                                        line,
                                        column
                                );
                        }
                        return book_lexer_emit(
                                state,
                                BOOK_TOKEN_GREATER,
                                start,
                                1,
                                line,
                                column
                        );

                default:
                        return book_lexer_fail(
                                state,
                                line,
                                column,
                                "unexpected byte 0x%02x",
                                value & 0xff
                        );
        }
}

int book_lexer_tokenize(
        const unsigned char * source,
        c_size_t source_length,
        BOOK_LEXER_RESULT * result
) {
        BOOK_LEXER_STATE state;

        if (!result) return ABNORMAL;
        memset(result, 0x00, sizeof(*result));

        if (!source && source_length > 0) {
                result->status = ABNORMAL;
                snprintf(
                        result->error,
                        sizeof(result->error),
                        "source is NULL"
                );
                result->error_line = 1;
                result->error_column = 1;
                return ABNORMAL;
        }

        if (source_length > BOOK_LEXER_SOURCE_LIMIT) {
                result->status = ABNORMAL;
                snprintf(
                        result->error,
                        sizeof(result->error),
                        "source exceeds %u byte lexer limit",
                        (unsigned int)BOOK_LEXER_SOURCE_LIMIT
                );
                result->error_line = 1;
                result->error_column = 1;
                return ABNORMAL;
        }

        result->source = calloc(source_length + 1U, 1);
        if (!result->source) {
                result->status = ABNORMAL;
                snprintf(
                        result->error,
                        sizeof(result->error),
                        "failed to allocate source buffer"
                );
                result->error_line = 1;
                result->error_column = 1;
                return ABNORMAL;
        }

        if (source_length > 0) {
                memcpy(result->source, source, source_length);
        }
        result->source_length = source_length;
        result->status = NORMAL;

        memset(&state, 0x00, sizeof(state));
        state.result = result;
        state.line = 1;
        state.column = 1;

        while (state.offset < result->source_length) {
                int value;

                if (book_lexer_skip_ignored(&state) != NORMAL) goto fail;
                if (state.offset >= result->source_length) break;

                value = book_lexer_peek(&state, 0);

                if (value == 0x00) {
                        (void)book_lexer_fail(
                                &state,
                                state.line,
                                state.column,
                                "NUL byte is not permitted directly in Lua source"
                        );
                        goto fail;
                }

                if (book_lexer_is_alpha(value) == ISTRUE) {
                        if (book_lexer_identifier(&state) != NORMAL) goto fail;
                        continue;
                }

                if (book_lexer_is_digit(value) == ISTRUE) {
                        if (book_lexer_number(&state) != NORMAL) goto fail;
                        continue;
                }

                if (value == '"' || value == '\'') {
                        if (book_lexer_string(&state) != NORMAL) goto fail;
                        continue;
                }

                if (book_lexer_operator(&state) != NORMAL) goto fail;
        }

        if (
                book_lexer_emit(
                        &state,
                        BOOK_TOKEN_EOF,
                        result->source_length,
                        0,
                        state.line,
                        state.column
                ) != NORMAL
        ) goto fail;

        result->status = NORMAL;
        return NORMAL;

fail:
        result->status = ABNORMAL;
        return ABNORMAL;
}

int book_lexer_tokenize_file(
        const unsigned char * path,
        BOOK_LEXER_RESULT * result
) {
        struct stat info;
        unsigned char * source = NULL;
        c_size_t source_length;
        c_size_t offset = 0;
        int fd;
        int status;

        if (!path || !result) return ABNORMAL;
        memset(result, 0x00, sizeof(*result));

        fd = open((const char*)path, O_RDONLY | O_NOFOLLOW);
        if (fd < 0) {
                result->status = ABNORMAL;
                result->error_line = 1;
                result->error_column = 1;
                snprintf(
                        result->error,
                        sizeof(result->error),
                        "failed to open Lua source"
                );
                return ABNORMAL;
        }

        memset(&info, 0x00, sizeof(info));
        if (
                fstat(fd, &info) != NORMAL
                || !S_ISREG(info.st_mode)
                || info.st_size < 0
                || (uint64_t)info.st_size > BOOK_LEXER_SOURCE_LIMIT
        ) {
                close(fd);
                result->status = ABNORMAL;
                result->error_line = 1;
                result->error_column = 1;
                snprintf(
                        result->error,
                        sizeof(result->error),
                        "Lua source is not a bounded regular file"
                );
                return ABNORMAL;
        }

        source_length = (c_size_t)info.st_size;
        source = calloc(source_length ? source_length : 1U, 1);
        if (!source) {
                close(fd);
                result->status = ABNORMAL;
                result->error_line = 1;
                result->error_column = 1;
                snprintf(
                        result->error,
                        sizeof(result->error),
                        "failed to allocate Lua source buffer"
                );
                return ABNORMAL;
        }

        while (offset < source_length) {
                ssize_t received = read(
                        fd,
                        source + offset,
                        source_length - offset
                );

                if (received < 0) {
                        if (errno == EINTR) continue;
                        free(source);
                        close(fd);
                        result->status = ABNORMAL;
                        result->error_line = 1;
                        result->error_column = 1;
                        snprintf(
                                result->error,
                                sizeof(result->error),
                                "failed to read Lua source"
                        );
                        return ABNORMAL;
                }

                if (received == 0) {
                        free(source);
                        close(fd);
                        result->status = ABNORMAL;
                        result->error_line = 1;
                        result->error_column = 1;
                        snprintf(
                                result->error,
                                sizeof(result->error),
                                "short read while loading Lua source"
                        );
                        return ABNORMAL;
                }

                offset += (c_size_t)received;
        }

        if (close(fd) != NORMAL) {
                free(source);
                result->status = ABNORMAL;
                result->error_line = 1;
                result->error_column = 1;
                snprintf(
                        result->error,
                        sizeof(result->error),
                        "failed to close Lua source"
                );
                return ABNORMAL;
        }

        status = book_lexer_tokenize(
                source,
                source_length,
                result
        );

        memset(source, 0x00, source_length);
        free(source);
        return status;
}

void book_lexer_free(BOOK_LEXER_RESULT * result) {
        if (!result) return;

        if (result->source) {
                memset(result->source, 0x00, result->source_length);
                free(result->source);
        }

        if (result->tokens) {
                memset(
                        result->tokens,
                        0x00,
                        result->capacity * sizeof(*result->tokens)
                );
                free(result->tokens);
        }

        memset(result, 0x00, sizeof(*result));
        return;
}

static int book_lexer_hex_value(int value) {
        if (value >= '0' && value <= '9') return value - '0';
        if (value >= 'a' && value <= 'f') return 10 + value - 'a';
        if (value >= 'A' && value <= 'F') return 10 + value - 'A';
        return -1;
}

int book_lexer_decode_string(
        const BOOK_LEXER_RESULT * result,
        const BOOK_TOKEN * token,
        unsigned char * output,
        c_size_t output_size,
        c_size_t * output_length
) {
        c_size_t cursor;
        c_size_t end;
        c_size_t written = 0;

        if (
                !result
                || !token
                || token->kind != BOOK_TOKEN_STRING
                || !output
                || !output_length
                || token->offset > result->source_length
                || token->length > result->source_length - token->offset
        ) {
                return ABNORMAL;
        }

        // Decoding never grows a validated short string. Reserve one extra byte
        // for a convenience terminator while output_length remains authoritative.
        if (output_size <= token->length) return ABNORMAL;

        cursor = token->offset;
        end = token->offset + token->length;

        while (cursor < end) {
                unsigned char value = result->source[cursor++];

                if (value != '\\') {
                        output[written++] = value;
                        continue;
                }

                if (cursor >= end) return ABNORMAL;
                value = result->source[cursor++];

                switch (value) {
                        case 'a': output[written++] = '\a'; break;
                        case 'b': output[written++] = '\b'; break;
                        case 'f': output[written++] = '\f'; break;
                        case 'n': output[written++] = '\n'; break;
                        case 'r': output[written++] = '\r'; break;
                        case 't': output[written++] = '\t'; break;
                        case 'v': output[written++] = '\v'; break;
                        case '\\': output[written++] = '\\'; break;
                        case '"': output[written++] = '"'; break;
                        case '\'': output[written++] = '\''; break;

                        case 'x': {
                                int high;
                                int low;

                                if (cursor + 1U >= end) return ABNORMAL;
                                high = book_lexer_hex_value(result->source[cursor]);
                                low = book_lexer_hex_value(result->source[cursor + 1U]);
                                if (high < 0 || low < 0) return ABNORMAL;

                                output[written++] = (unsigned char)(
                                        (high << 4) | low
                                );
                                cursor += 2U;
                                break;
                        }

                        case 'z':
                                while (
                                        cursor < end
                                        && book_lexer_is_space(
                                                result->source[cursor]
                                        ) == ISTRUE
                                ) {
                                        if (
                                                result->source[cursor] == '\r'
                                                && cursor + 1U < end
                                                && result->source[cursor + 1U] == '\n'
                                        ) {
                                                cursor += 2U;
                                        } else {
                                                cursor++;
                                        }
                                }
                                break;

                        case '\r':
                                if (
                                        cursor < end
                                        && result->source[cursor] == '\n'
                                ) {
                                        cursor++;
                                }
                                output[written++] = '\n';
                                break;

                        case '\n':
                                output[written++] = '\n';
                                break;

                        default:
                                if (book_lexer_is_digit(value) == ISTRUE) {
                                        unsigned int decimal = (unsigned int)(value - '0');
                                        unsigned int digits = 1;

                                        while (
                                                digits < 3U
                                                && cursor < end
                                                && book_lexer_is_digit(
                                                        result->source[cursor]
                                                ) == ISTRUE
                                        ) {
                                                decimal = (decimal * 10U)
                                                        + (unsigned int)(
                                                                result->source[cursor] - '0'
                                                        );
                                                cursor++;
                                                digits++;
                                        }

                                        if (decimal > 255U) return ABNORMAL;
                                        output[written++] = (unsigned char)decimal;
                                        break;
                                }
                                return ABNORMAL;
                }
        }

        output[written] = 0x00;
        *output_length = written;
        return NORMAL;
}

const char * book_token_kind_name(book_token_kind_t kind) {
        switch (kind) {
                case BOOK_TOKEN_EOF:           return "EOF";
                case BOOK_TOKEN_IDENTIFIER:    return "IDENTIFIER";
                case BOOK_TOKEN_NUMBER:        return "NUMBER";
                case BOOK_TOKEN_STRING:        return "STRING";
                case BOOK_TOKEN_AND:           return "and";
                case BOOK_TOKEN_BREAK:         return "break";
                case BOOK_TOKEN_DO:            return "do";
                case BOOK_TOKEN_ELSE:          return "else";
                case BOOK_TOKEN_ELSEIF:        return "elseif";
                case BOOK_TOKEN_END:           return "end";
                case BOOK_TOKEN_FALSE:         return "false";
                case BOOK_TOKEN_FOR:           return "for";
                case BOOK_TOKEN_FUNCTION:      return "function";
                case BOOK_TOKEN_IF:            return "if";
                case BOOK_TOKEN_IN:            return "in";
                case BOOK_TOKEN_LOCAL:         return "local";
                case BOOK_TOKEN_NIL:           return "nil";
                case BOOK_TOKEN_NOT:           return "not";
                case BOOK_TOKEN_OR:            return "or";
                case BOOK_TOKEN_RETURN:        return "return";
                case BOOK_TOKEN_THEN:          return "then";
                case BOOK_TOKEN_TRUE:          return "true";
                case BOOK_TOKEN_WHILE:         return "while";
                case BOOK_TOKEN_PLUS:          return "+";
                case BOOK_TOKEN_MINUS:         return "-";
                case BOOK_TOKEN_STAR:          return "*";
                case BOOK_TOKEN_SLASH:         return "/";
                case BOOK_TOKEN_FLOOR_DIV:     return "//";
                case BOOK_TOKEN_PERCENT:       return "%";
                case BOOK_TOKEN_CONCAT:        return "..";
                case BOOK_TOKEN_LENGTH:        return "#";
                case BOOK_TOKEN_TILDE:         return "~";
                case BOOK_TOKEN_EQUAL:         return "=";
                case BOOK_TOKEN_EQUAL_EQUAL:   return "==";
                case BOOK_TOKEN_NOT_EQUAL:     return "~=";
                case BOOK_TOKEN_LESS:          return "<";
                case BOOK_TOKEN_LESS_EQUAL:    return "<=";
                case BOOK_TOKEN_GREATER:       return ">";
                case BOOK_TOKEN_GREATER_EQUAL: return ">=";
                case BOOK_TOKEN_BIT_AND:       return "&";
                case BOOK_TOKEN_BIT_OR:        return "|";
                case BOOK_TOKEN_SHIFT_LEFT:    return "<<";
                case BOOK_TOKEN_SHIFT_RIGHT:   return ">>";
                case BOOK_TOKEN_LEFT_PAREN:    return "(";
                case BOOK_TOKEN_RIGHT_PAREN:   return ")";
                case BOOK_TOKEN_LEFT_BRACE:    return "{";
                case BOOK_TOKEN_RIGHT_BRACE:   return "}";
                case BOOK_TOKEN_LEFT_BRACKET:  return "[";
                case BOOK_TOKEN_RIGHT_BRACKET: return "]";
                case BOOK_TOKEN_COMMA:         return ",";
                case BOOK_TOKEN_DOT:           return ".";
                case BOOK_TOKEN_COLON:         return ":";
                case BOOK_TOKEN_SEMICOLON:     return ";";
                case BOOK_TOKEN_ELLIPSIS:      return "...";
                default:                       return "UNKNOWN";
        }
}
