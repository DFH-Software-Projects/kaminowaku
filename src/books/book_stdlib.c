// Copyright 2026 Jamison A. Drapeau
#include "book_stdlib.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BOOK_STDLIB_BUFFER_LIMIT   (1024U * 1024U)
#define BOOK_PATTERN_CAPTURE_LIMIT 16U

typedef struct BOOK_BUFFER {
        unsigned char * data;
        c_size_t length;
        c_size_t capacity;
} BOOK_BUFFER;

typedef struct BOOK_PATTERN_CAPTURE {
        c_size_t start;
        c_size_t end;
} BOOK_PATTERN_CAPTURE;

typedef struct BOOK_PATTERN_MATCH {
        c_size_t start;
        c_size_t end;
        BOOK_PATTERN_CAPTURE captures[BOOK_PATTERN_CAPTURE_LIMIT];
        c_size_t capture_count;
} BOOK_PATTERN_MATCH;

static void book_std_multi_clear(BOOK_MULTI_VALUE * values) {
        if (values) memset(values, 0x00, sizeof(*values));
}

static int book_std_return(
        BOOK_MULTI_VALUE * returns,
        BOOK_VALUE value
) {
        if (!returns) return ABNORMAL;
        book_std_multi_clear(returns);
        returns->values[0] = value;
        returns->count = 1;
        return NORMAL;
}

static int book_std_return2(
        BOOK_MULTI_VALUE * returns,
        BOOK_VALUE first,
        BOOK_VALUE second
) {
        if (!returns) return ABNORMAL;
        book_std_multi_clear(returns);
        returns->values[0] = first;
        returns->values[1] = second;
        returns->count = 2;
        return NORMAL;
}

static int book_std_string_arg(
        BOOK_RUNTIME * runtime,
        const BOOK_VALUE * arguments,
        c_size_t count,
        c_size_t index,
        const unsigned char ** bytes,
        c_size_t * length,
        const char * message
) {
        if (
                index >= count
                || book_value_string(
                        &arguments[index],
                        bytes,
                        length
                ) != NORMAL
        ) {
                return book_runtime_native_fail(runtime, message);
        }

        return NORMAL;
}

static int book_std_number_arg(
        BOOK_RUNTIME * runtime,
        const BOOK_VALUE * arguments,
        c_size_t count,
        c_size_t index,
        double * number,
        const char * message
) {
        if (
                index >= count
                || book_value_number(
                        &arguments[index],
                        number
                ) != NORMAL
        ) {
                return book_runtime_native_fail(runtime, message);
        }

        return NORMAL;
}

static int book_std_integer(
        BOOK_RUNTIME * runtime,
        double number,
        int64_t * integer,
        const char * message
) {
        int64_t value;

        if (!integer) return ABNORMAL;

        if (
                number < -9223372036854775808.0
                || number >= 9223372036854775808.0
        ) {
                return book_runtime_native_fail(runtime, message);
        }

        value = (int64_t)number;
        if ((double)value != number) {
                return book_runtime_native_fail(runtime, message);
        }

        *integer = value;
        return NORMAL;
}

static int book_buffer_reserve(
        BOOK_BUFFER * buffer,
        c_size_t extra
) {
        c_size_t required;
        c_size_t next;
        unsigned char * expanded;

        if (!buffer) return ABNORMAL;
        if (extra > BOOK_STDLIB_BUFFER_LIMIT - buffer->length) return ABNORMAL;

        required = buffer->length + extra;
        if (required <= buffer->capacity) return NORMAL;

        next = buffer->capacity ? buffer->capacity : 128U;
        while (next < required) {
                if (next > BOOK_STDLIB_BUFFER_LIMIT / 2U) {
                        next = BOOK_STDLIB_BUFFER_LIMIT;
                        break;
                }
                next *= 2U;
        }

        if (next < required || next > BOOK_STDLIB_BUFFER_LIMIT) return ABNORMAL;

        expanded = realloc(buffer->data, next);
        if (!expanded) return ABNORMAL;

        buffer->data = expanded;
        buffer->capacity = next;
        return NORMAL;
}

static int book_buffer_append(
        BOOK_BUFFER * buffer,
        const unsigned char * data,
        c_size_t length
) {
        if (!buffer || (!data && length > 0)) return ABNORMAL;
        if (book_buffer_reserve(buffer, length) != NORMAL) return ABNORMAL;

        if (length > 0) {
                memcpy(buffer->data + buffer->length, data, length);
        }
        buffer->length += length;
        return NORMAL;
}

static int book_buffer_byte(
        BOOK_BUFFER * buffer,
        unsigned char value
) {
        return book_buffer_append(buffer, &value, 1);
}

static void book_buffer_free(BOOK_BUFFER * buffer) {
        if (!buffer) return;
        if (buffer->data) {
                memset(buffer->data, 0x00, buffer->capacity);
                free(buffer->data);
        }
        memset(buffer, 0x00, sizeof(*buffer));
}

static int book_std_make_string(
        BOOK_RUNTIME * runtime,
        BOOK_BUFFER * buffer,
        BOOK_VALUE * value
) {
        int status;

        if (!runtime || !buffer || !value) return ABNORMAL;

        status = book_runtime_make_string(
                runtime,
                buffer->data,
                buffer->length,
                value
        );
        return status;
}

static int64_t book_std_pos_relative(
        int64_t position,
        c_size_t length
) {
        if (position >= 0) return position;
        return (int64_t)length + position + 1;
}

static int book_std_value_to_string(
        BOOK_RUNTIME * runtime,
        BOOK_VALUE value,
        BOOK_VALUE * string_value
) {
        const unsigned char * bytes;
        c_size_t length;
        char buffer[96];
        int written;
        const char * text;

        if (!runtime || !string_value) return ABNORMAL;

        if (value.type == BOOK_VALUE_STRING) {
                *string_value = value;
                return NORMAL;
        }

        switch (value.type) {
                case BOOK_VALUE_NIL:
                        text = "nil";
                        return book_runtime_make_string(
                                runtime,
                                (const unsigned char*)text,
                                3,
                                string_value
                        );

                case BOOK_VALUE_BOOLEAN:
                        text = value.as.boolean == ISTRUE ? "true" : "false";
                        return book_runtime_make_string(
                                runtime,
                                (const unsigned char*)text,
                                value.as.boolean == ISTRUE ? 4 : 5,
                                string_value
                        );

                case BOOK_VALUE_NUMBER:
                        written = snprintf(
                                buffer,
                                sizeof(buffer),
                                "%.14g",
                                value.as.number
                        );
                        if (written <= 0 || written >= (int)sizeof(buffer)) {
                                return book_runtime_native_fail(
                                        runtime,
                                        "failed to format Lua number"
                                );
                        }
                        return book_runtime_make_string(
                                runtime,
                                (const unsigned char*)buffer,
                                (c_size_t)written,
                                string_value
                        );

                case BOOK_VALUE_TABLE:
                        text = "table";
                        break;

                case BOOK_VALUE_FUNCTION:
                case BOOK_VALUE_NATIVE_FUNCTION:
                        text = "function";
                        break;

                default:
                        text = "unknown";
                        break;
        }

        bytes = (const unsigned char*)text;
        length = (c_size_t)strlen(text);
        return book_runtime_make_string(
                runtime,
                bytes,
                length,
                string_value
        );
}

// -----------------------------------------------------------------------------
// Base library
// -----------------------------------------------------------------------------

static int book_std_assert(
        BOOK_RUNTIME * runtime,
        const BOOK_VALUE * arguments,
        c_size_t count,
        BOOK_MULTI_VALUE * returns
) {
        const unsigned char * message_bytes = NULL;
        c_size_t message_length = 0;
        char message[BOOK_RUNTIME_ERROR_BLOCK];

        if (!runtime || !returns) return ABNORMAL;

        if (
                count > 0
                && book_runtime_truthy_value(&arguments[0]) == ISTRUE
        ) {
                if (count > BOOK_RUNTIME_MULTI_LIMIT) {
                        return book_runtime_native_fail(
                                runtime,
                                "assert return limit exceeded"
                        );
                }

                book_std_multi_clear(returns);
                for (c_size_t i = 0; i < count; i++) {
                        returns->values[i] = arguments[i];
                }
                returns->count = count;
                return NORMAL;
        }

        if (
                count > 1
                && book_value_string(
                        &arguments[1],
                        &message_bytes,
                        &message_length
                ) == NORMAL
        ) {
                c_size_t copy = message_length < sizeof(message) - 1U
                        ? message_length
                        : sizeof(message) - 1U;

                memcpy(message, message_bytes, copy);
                message[copy] = 0x00;
                return book_runtime_native_fail(runtime, message);
        }

        return book_runtime_native_fail(runtime, "assertion failed!");
}

static int book_std_error(
        BOOK_RUNTIME * runtime,
        const BOOK_VALUE * arguments,
        c_size_t count,
        BOOK_MULTI_VALUE * returns
) {
        BOOK_VALUE string;
        const unsigned char * bytes;
        c_size_t length;
        char message[BOOK_RUNTIME_ERROR_BLOCK];

        (void)returns;

        if (!runtime) return ABNORMAL;

        if (count == 0) {
                return book_runtime_native_fail(runtime, "Lua error");
        }

        if (
                book_std_value_to_string(
                        runtime,
                        arguments[0],
                        &string
                ) != NORMAL
                || book_value_string(
                        &string,
                        &bytes,
                        &length
                ) != NORMAL
        ) {
                return ABNORMAL;
        }

        length = length < sizeof(message) - 1U
                ? length
                : sizeof(message) - 1U;

        memcpy(message, bytes, length);
        message[length] = 0x00;
        return book_runtime_native_fail(runtime, message);
}

static int book_std_type(
        BOOK_RUNTIME * runtime,
        const BOOK_VALUE * arguments,
        c_size_t count,
        BOOK_MULTI_VALUE * returns
) {
        const char * name;
        BOOK_VALUE value;

        if (!runtime || !returns || count == 0) {
                return book_runtime_native_fail(runtime, "type() requires a value");
        }

        name = book_value_type_name(arguments[0].type);
        if (
                arguments[0].type == BOOK_VALUE_NATIVE_FUNCTION
                || arguments[0].type == BOOK_VALUE_FUNCTION
        ) {
                name = "function";
        }

        if (
                book_runtime_make_string(
                        runtime,
                        (const unsigned char*)name,
                        (c_size_t)strlen(name),
                        &value
                ) != NORMAL
        ) {
                return ABNORMAL;
        }

        return book_std_return(returns, value);
}

static int book_std_tostring(
        BOOK_RUNTIME * runtime,
        const BOOK_VALUE * arguments,
        c_size_t count,
        BOOK_MULTI_VALUE * returns
) {
        BOOK_VALUE value;

        if (!runtime || !returns || count == 0) {
                return book_runtime_native_fail(
                        runtime,
                        "tostring() requires a value"
                );
        }

        if (
                book_std_value_to_string(
                        runtime,
                        arguments[0],
                        &value
                ) != NORMAL
        ) {
                return ABNORMAL;
        }

        return book_std_return(returns, value);
}

static int book_std_digit_value(unsigned char value) {
        if (value >= '0' && value <= '9') return (int)(value - '0');
        if (value >= 'a' && value <= 'z') return 10 + (int)(value - 'a');
        if (value >= 'A' && value <= 'Z') return 10 + (int)(value - 'A');
        return -1;
}

static int book_std_tonumber(
        BOOK_RUNTIME * runtime,
        const BOOK_VALUE * arguments,
        c_size_t count,
        BOOK_MULTI_VALUE * returns
) {
        const unsigned char * bytes;
        c_size_t length;
        int64_t base = 10;

        if (!runtime || !returns || count == 0) {
                return book_runtime_native_fail(
                        runtime,
                        "tonumber() requires a value"
                );
        }

        if (
                count == 1
                && arguments[0].type == BOOK_VALUE_NUMBER
        ) {
                return book_std_return(returns, arguments[0]);
        }

        if (
                book_value_string(
                        &arguments[0],
                        &bytes,
                        &length
                ) != NORMAL
        ) {
                return book_std_return(returns, book_runtime_nil());
        }

        if (count > 1) {
                double base_number;

                if (
                        book_value_number(
                                &arguments[1],
                                &base_number
                        ) != NORMAL
                        || book_std_integer(
                                runtime,
                                base_number,
                                &base,
                                "tonumber base must be an integer"
                        ) != NORMAL
                ) {
                        return ABNORMAL;
                }

                if (base < 2 || base > 36) {
                        return book_runtime_native_fail(
                                runtime,
                                "tonumber base must be between 2 and 36"
                        );
                }
        }

        while (length > 0 && isspace((unsigned char)*bytes)) {
                bytes++;
                length--;
        }
        while (
                length > 0
                && isspace((unsigned char)bytes[length - 1U])
        ) {
                length--;
        }

        if (length == 0) {
                return book_std_return(returns, book_runtime_nil());
        }

        if (count > 1 || base != 10) {
                int negative = ISFALSE;
                uint64_t value = 0;
                c_size_t index = 0;

                if (bytes[index] == '+' || bytes[index] == '-') {
                        negative = bytes[index] == '-' ? ISTRUE : ISFALSE;
                        index++;
                }

                if (index >= length) {
                        return book_std_return(returns, book_runtime_nil());
                }

                for (; index < length; index++) {
                        int digit = book_std_digit_value(bytes[index]);

                        if (digit < 0 || digit >= base) {
                                return book_std_return(
                                        returns,
                                        book_runtime_nil()
                                );
                        }

                        if (
                                value > (
                                        UINT64_MAX - (uint64_t)digit
                                ) / (uint64_t)base
                        ) {
                                return book_std_return(
                                        returns,
                                        book_runtime_nil()
                                );
                        }

                        value = value * (uint64_t)base + (uint64_t)digit;
                }

                return book_std_return(
                        returns,
                        book_runtime_number(
                                negative == ISTRUE
                                        ? -(double)value
                                        : (double)value
                        )
                );
        }

        {
                char buffer[256];
                char * end = NULL;
                double value;

                if (length >= sizeof(buffer)) {
                        return book_std_return(
                                returns,
                                book_runtime_nil()
                        );
                }

                memcpy(buffer, bytes, length);
                buffer[length] = 0x00;
                errno = 0;
                value = strtod(buffer, &end);

                if (
                        errno != 0
                        || !end
                        || *end != 0x00
                ) {
                        return book_std_return(
                                returns,
                                book_runtime_nil()
                        );
                }

                return book_std_return(
                        returns,
                        book_runtime_number(value)
                );
        }
}

static int book_std_pairs_iterator(
        BOOK_RUNTIME * runtime,
        const BOOK_VALUE * arguments,
        c_size_t count,
        BOOK_MULTI_VALUE * returns
) {
        BOOK_VALUE table;
        BOOK_VALUE index_value;
        double index_number = 0.0;
        int64_t index = 0;
        BOOK_VALUE key;
        BOOK_VALUE value;

        if (
                !runtime
                || !returns
                || count < 1
                || arguments[0].type != BOOK_VALUE_TABLE
        ) {
                return book_runtime_native_fail(
                        runtime,
                        "invalid pairs iterator state"
                );
        }

        if (
                book_runtime_table_get_string_value(
                        runtime,
                        arguments[0],
                        "_table",
                        &table
                ) != NORMAL
                || table.type != BOOK_VALUE_TABLE
                || book_runtime_table_get_string_value(
                        runtime,
                        arguments[0],
                        "_index",
                        &index_value
                ) != NORMAL
                || book_value_number(
                        &index_value,
                        &index_number
                ) != NORMAL
                || book_std_integer(
                        runtime,
                        index_number,
                        &index,
                        "invalid pairs iterator index"
                ) != NORMAL
        ) {
                return ABNORMAL;
        }

        if (
                index < 0
                || (c_size_t)index >= book_runtime_table_count_value(&table)
        ) {
                book_std_multi_clear(returns);
                return NORMAL;
        }

        if (
                book_runtime_table_entry_value(
                        &table,
                        (c_size_t)index,
                        &key,
                        &value
                ) != NORMAL
                || book_runtime_table_set_string(
                        runtime,
                        arguments[0],
                        "_index",
                        book_runtime_number((double)(index + 1))
                ) != NORMAL
        ) {
                return ABNORMAL;
        }

        return book_std_return2(returns, key, value);
}

static int book_std_pairs(
        BOOK_RUNTIME * runtime,
        const BOOK_VALUE * arguments,
        c_size_t count,
        BOOK_MULTI_VALUE * returns
) {
        BOOK_VALUE state;
        BOOK_VALUE iterator;

        if (
                !runtime
                || !returns
                || count < 1
                || arguments[0].type != BOOK_VALUE_TABLE
        ) {
                return book_runtime_native_fail(
                        runtime,
                        "pairs() requires a table"
                );
        }

        if (
                book_runtime_make_table(runtime, &state) != NORMAL
                || book_runtime_table_set_string(
                        runtime,
                        state,
                        "_table",
                        arguments[0]
                ) != NORMAL
                || book_runtime_table_set_string(
                        runtime,
                        state,
                        "_index",
                        book_runtime_number(0)
                ) != NORMAL
                || book_runtime_make_native(
                        runtime,
                        "pairs.iterator",
                        book_std_pairs_iterator,
                        &iterator
                ) != NORMAL
        ) {
                return ABNORMAL;
        }

        book_std_multi_clear(returns);
        returns->values[0] = iterator;
        returns->values[1] = state;
        returns->values[2] = book_runtime_nil();
        returns->count = 3;
        return NORMAL;
}

static int book_std_ipairs_iterator(
        BOOK_RUNTIME * runtime,
        const BOOK_VALUE * arguments,
        c_size_t count,
        BOOK_MULTI_VALUE * returns
) {
        BOOK_VALUE key;
        BOOK_VALUE value;
        double control_number = 0.0;
        int64_t control = 0;

        if (
                !runtime
                || !returns
                || count < 1
                || arguments[0].type != BOOK_VALUE_TABLE
        ) {
                return book_runtime_native_fail(
                        runtime,
                        "invalid ipairs iterator state"
                );
        }

        if (
                count > 1
                && arguments[1].type != BOOK_VALUE_NIL
        ) {
                if (
                        book_value_number(
                                &arguments[1],
                                &control_number
                        ) != NORMAL
                        || book_std_integer(
                                runtime,
                                control_number,
                                &control,
                                "invalid ipairs control value"
                        ) != NORMAL
                ) {
                        return ABNORMAL;
                }
        }

        control++;
        key = book_runtime_number((double)control);

        if (
                book_runtime_table_get_value(
                        runtime,
                        arguments[0],
                        &key,
                        &value
                ) != NORMAL
        ) {
                return ABNORMAL;
        }

        if (value.type == BOOK_VALUE_NIL) {
                book_std_multi_clear(returns);
                return NORMAL;
        }

        return book_std_return2(returns, key, value);
}

static int book_std_ipairs(
        BOOK_RUNTIME * runtime,
        const BOOK_VALUE * arguments,
        c_size_t count,
        BOOK_MULTI_VALUE * returns
) {
        BOOK_VALUE iterator;

        if (
                !runtime
                || !returns
                || count < 1
                || arguments[0].type != BOOK_VALUE_TABLE
        ) {
                return book_runtime_native_fail(
                        runtime,
                        "ipairs() requires a table"
                );
        }

        if (
                book_runtime_make_native(
                        runtime,
                        "ipairs.iterator",
                        book_std_ipairs_iterator,
                        &iterator
                ) != NORMAL
        ) {
                return ABNORMAL;
        }

        book_std_multi_clear(returns);
        returns->values[0] = iterator;
        returns->values[1] = arguments[0];
        returns->values[2] = book_runtime_number(0);
        returns->count = 3;
        return NORMAL;
}

// -----------------------------------------------------------------------------
// Lua pattern subset (binary-safe)
// -----------------------------------------------------------------------------

static int book_pattern_class(
        unsigned char value,
        unsigned char class_name
) {
        int result;

        switch ((unsigned char)tolower(class_name)) {
                case 'a': result = isalpha(value) ? ISTRUE : ISFALSE; break;
                case 'c': result = iscntrl(value) ? ISTRUE : ISFALSE; break;
                case 'd': result = isdigit(value) ? ISTRUE : ISFALSE; break;
                case 'l': result = islower(value) ? ISTRUE : ISFALSE; break;
                case 'p': result = ispunct(value) ? ISTRUE : ISFALSE; break;
                case 's': result = isspace(value) ? ISTRUE : ISFALSE; break;
                case 'u': result = isupper(value) ? ISTRUE : ISFALSE; break;
                case 'w': result = isalnum(value) ? ISTRUE : ISFALSE; break;
                case 'x': result = isxdigit(value) ? ISTRUE : ISFALSE; break;
                case 'z': result = value == 0x00 ? ISTRUE : ISFALSE; break;
                default:  result = value == class_name ? ISTRUE : ISFALSE; break;
        }

        if (
                class_name >= 'A'
                && class_name <= 'Z'
                && tolower(class_name) != class_name
        ) {
                result = result == ISTRUE ? ISFALSE : ISTRUE;
        }

        return result;
}

static int book_pattern_class_end(
        const unsigned char * pattern,
        c_size_t length,
        c_size_t start,
        c_size_t * end
) {
        c_size_t cursor = start + 1U;

        if (!pattern || !end || start >= length || pattern[start] != '[') {
                return ABNORMAL;
        }

        if (cursor < length && pattern[cursor] == '^') cursor++;

        for (; cursor < length; cursor++) {
                if (pattern[cursor] == '%' && cursor + 1U < length) {
                        cursor++;
                        continue;
                }

                if (pattern[cursor] == ']') {
                        *end = cursor + 1U;
                        return NORMAL;
                }
        }

        return ABNORMAL;
}

static int book_pattern_set_match(
        unsigned char value,
        const unsigned char * pattern,
        c_size_t start,
        c_size_t end
) {
        c_size_t cursor = start + 1U;
        int negative = ISFALSE;
        int matched = ISFALSE;

        if (cursor < end && pattern[cursor] == '^') {
                negative = ISTRUE;
                cursor++;
        }

        while (cursor + 1U < end) {
                unsigned char first = pattern[cursor];

                if (first == '%') {
                        if (cursor + 1U >= end - 1U) break;
                        if (
                                book_pattern_class(
                                        value,
                                        pattern[cursor + 1U]
                                ) == ISTRUE
                        ) {
                                matched = ISTRUE;
                        }
                        cursor += 2U;
                        continue;
                }

                if (
                        cursor + 2U < end - 1U
                        && pattern[cursor + 1U] == '-'
                ) {
                        unsigned char last = pattern[cursor + 2U];

                        if (value >= first && value <= last) {
                                matched = ISTRUE;
                        }
                        cursor += 3U;
                        continue;
                }

                if (value == first) matched = ISTRUE;
                cursor++;
        }

        return negative == ISTRUE
                ? (matched == ISTRUE ? ISFALSE : ISTRUE)
                : matched;
}

static int book_pattern_atom(
        unsigned char value,
        const unsigned char * pattern,
        c_size_t length,
        c_size_t start,
        c_size_t * next
) {
        unsigned char p;

        if (!pattern || !next || start >= length) return ISFALSE;
        p = pattern[start];

        if (p == '.') {
                *next = start + 1U;
                return ISTRUE;
        }

        if (p == '%') {
                if (start + 1U >= length) return ISFALSE;
                *next = start + 2U;
                return book_pattern_class(value, pattern[start + 1U]);
        }

        if (p == '[') {
                c_size_t end;

                if (
                        book_pattern_class_end(
                                pattern,
                                length,
                                start,
                                &end
                        ) != NORMAL
                ) {
                        return ISFALSE;
                }

                *next = end;
                return book_pattern_set_match(
                        value,
                        pattern,
                        start,
                        end
                );
        }

        *next = start + 1U;
        return value == p ? ISTRUE : ISFALSE;
}

static int book_pattern_find_close(
        const unsigned char * pattern,
        c_size_t start,
        c_size_t end,
        c_size_t * close
) {
        unsigned int depth = 1;

        for (c_size_t cursor = start; cursor < end; cursor++) {
                if (pattern[cursor] == '%' && cursor + 1U < end) {
                        cursor++;
                        continue;
                }

                if (pattern[cursor] == '[') {
                        c_size_t class_end;

                        if (
                                book_pattern_class_end(
                                        pattern,
                                        end,
                                        cursor,
                                        &class_end
                                ) != NORMAL
                        ) {
                                return ABNORMAL;
                        }
                        cursor = class_end - 1U;
                        continue;
                }

                if (pattern[cursor] == '(') depth++;
                else if (pattern[cursor] == ')') {
                        depth--;
                        if (depth == 0) {
                                *close = cursor;
                                return NORMAL;
                        }
                }
        }

        return ABNORMAL;
}

static int book_pattern_match_range(
        const unsigned char * subject,
        c_size_t subject_length,
        c_size_t subject_index,
        const unsigned char * pattern,
        c_size_t pattern_start,
        c_size_t pattern_end,
        BOOK_PATTERN_MATCH * captures,
        c_size_t * result_index
) {
        c_size_t cursor = pattern_start;

        if (
                !subject
                || !pattern
                || !captures
                || !result_index
        ) {
                return ISFALSE;
        }

        if (cursor >= pattern_end) {
                *result_index = subject_index;
                return ISTRUE;
        }

        if (
                pattern[cursor] == '$'
                && cursor + 1U == pattern_end
        ) {
                if (subject_index == subject_length) {
                        *result_index = subject_index;
                        return ISTRUE;
                }
                return ISFALSE;
        }

        if (pattern[cursor] == '(') {
                c_size_t close;
                c_size_t captured_end;
                c_size_t capture_index;
                BOOK_PATTERN_MATCH saved = *captures;

                if (
                        captures->capture_count >= BOOK_PATTERN_CAPTURE_LIMIT
                        || book_pattern_find_close(
                                pattern,
                                cursor + 1U,
                                pattern_end,
                                &close
                        ) != NORMAL
                ) {
                        return ISFALSE;
                }

                capture_index = captures->capture_count++;
                captures->captures[capture_index].start = subject_index;

                if (
                        book_pattern_match_range(
                                subject,
                                subject_length,
                                subject_index,
                                pattern,
                                cursor + 1U,
                                close,
                                captures,
                                &captured_end
                        ) != ISTRUE
                ) {
                        *captures = saved;
                        return ISFALSE;
                }

                captures->captures[capture_index].end = captured_end;

                if (
                        book_pattern_match_range(
                                subject,
                                subject_length,
                                captured_end,
                                pattern,
                                close + 1U,
                                pattern_end,
                                captures,
                                result_index
                        ) == ISTRUE
                ) {
                        return ISTRUE;
                }

                *captures = saved;
                return ISFALSE;
        }

        {
                c_size_t atom_end = cursor;
                c_size_t scan = subject_index;
                c_size_t maximum = 0;
                unsigned char quantifier = 0;

                if (
                        subject_index < subject_length
                        && book_pattern_atom(
                                subject[subject_index],
                                pattern,
                                pattern_end,
                                cursor,
                                &atom_end
                        ) == ISTRUE
                ) {
                        scan++;
                        maximum = 1;

                        while (
                                scan < subject_length
                                && book_pattern_atom(
                                        subject[scan],
                                        pattern,
                                        pattern_end,
                                        cursor,
                                        &atom_end
                                ) == ISTRUE
                        ) {
                                scan++;
                                maximum++;
                        }
                } else {
                        // Obtain atom width even if it does not match.
                        unsigned char dummy = 0;
                        if (
                                book_pattern_atom(
                                        dummy,
                                        pattern,
                                        pattern_end,
                                        cursor,
                                        &atom_end
                                ) != ISTRUE
                                && pattern[cursor] == '['
                        ) {
                                if (
                                        book_pattern_class_end(
                                                pattern,
                                                pattern_end,
                                                cursor,
                                                &atom_end
                                        ) != NORMAL
                                ) {
                                        return ISFALSE;
                                }
                        } else if (pattern[cursor] == '%') {
                                atom_end = cursor + 2U;
                        } else {
                                atom_end = cursor + 1U;
                        }
                }

                if (atom_end < pattern_end) {
                        unsigned char candidate = pattern[atom_end];
                        if (
                                candidate == '*'
                                || candidate == '+'
                                || candidate == '?'
                                || candidate == '-'
                        ) {
                                quantifier = candidate;
                        }
                }

                if (!quantifier) {
                        if (maximum == 0) return ISFALSE;

                        return book_pattern_match_range(
                                subject,
                                subject_length,
                                subject_index + 1U,
                                pattern,
                                atom_end,
                                pattern_end,
                                captures,
                                result_index
                        );
                }

                {
                        c_size_t minimum = quantifier == '+' ? 1U : 0U;
                        c_size_t limit = quantifier == '?' && maximum > 1U
                                ? 1U
                                : maximum;
                        c_size_t remainder = atom_end + 1U;

                        if (limit < minimum) return ISFALSE;

                        if (quantifier == '-') {
                                for (
                                        c_size_t count = minimum;
                                        count <= limit;
                                        count++
                                ) {
                                        BOOK_PATTERN_MATCH trial = *captures;

                                        if (
                                                book_pattern_match_range(
                                                        subject,
                                                        subject_length,
                                                        subject_index + count,
                                                        pattern,
                                                        remainder,
                                                        pattern_end,
                                                        &trial,
                                                        result_index
                                                ) == ISTRUE
                                        ) {
                                                *captures = trial;
                                                return ISTRUE;
                                        }
                                }
                        } else {
                                for (
                                        c_size_t count = limit + 1U;
                                        count-- > minimum;
                                ) {
                                        BOOK_PATTERN_MATCH trial = *captures;

                                        if (
                                                book_pattern_match_range(
                                                        subject,
                                                        subject_length,
                                                        subject_index + count,
                                                        pattern,
                                                        remainder,
                                                        pattern_end,
                                                        &trial,
                                                        result_index
                                                ) == ISTRUE
                                        ) {
                                                *captures = trial;
                                                return ISTRUE;
                                        }

                                        if (count == minimum) break;
                                }
                        }
                }
        }

        return ISFALSE;
}

static int book_pattern_search(
        const unsigned char * subject,
        c_size_t subject_length,
        const unsigned char * pattern,
        c_size_t pattern_length,
        c_size_t start,
        BOOK_PATTERN_MATCH * match
) {
        c_size_t pattern_start = 0;
        int anchored = ISFALSE;

        if (!subject || !pattern || !match) return ISFALSE;
        if (start > subject_length) return ISFALSE;

        if (pattern_length > 0 && pattern[0] == '^') {
                anchored = ISTRUE;
                pattern_start = 1;
        }

        for (
                c_size_t position = anchored == ISTRUE ? 0 : start;
                position <= subject_length;
                position++
        ) {
                BOOK_PATTERN_MATCH trial;
                c_size_t end;

                if (anchored == ISTRUE && position != 0) break;
                memset(&trial, 0x00, sizeof(trial));

                if (
                        book_pattern_match_range(
                                subject,
                                subject_length,
                                position,
                                pattern,
                                pattern_start,
                                pattern_length,
                                &trial,
                                &end
                        ) == ISTRUE
                ) {
                        trial.start = position;
                        trial.end = end;
                        *match = trial;
                        return ISTRUE;
                }
        }

        return ISFALSE;
}

static int book_pattern_return_match(
        BOOK_RUNTIME * runtime,
        const unsigned char * subject,
        const BOOK_PATTERN_MATCH * match,
        BOOK_MULTI_VALUE * returns,
        int8_t whole_if_no_captures
) {
        if (!runtime || !subject || !match || !returns) return ABNORMAL;
        book_std_multi_clear(returns);

        if (match->capture_count > 0) {
                for (c_size_t i = 0; i < match->capture_count; i++) {
                        BOOK_VALUE value;
                        c_size_t length = match->captures[i].end
                                - match->captures[i].start;

                        if (
                                book_runtime_make_string(
                                        runtime,
                                        subject + match->captures[i].start,
                                        length,
                                        &value
                                ) != NORMAL
                        ) {
                                return ABNORMAL;
                        }

                        returns->values[returns->count++] = value;
                }

                return NORMAL;
        }

        if (whole_if_no_captures == ISTRUE) {
                BOOK_VALUE value;

                if (
                        book_runtime_make_string(
                                runtime,
                                subject + match->start,
                                match->end - match->start,
                                &value
                        ) != NORMAL
                ) {
                        return ABNORMAL;
                }

                returns->values[0] = value;
                returns->count = 1;
        }

        return NORMAL;
}

// -----------------------------------------------------------------------------
// String library
// -----------------------------------------------------------------------------

static int book_std_string_byte(
        BOOK_RUNTIME * runtime,
        const BOOK_VALUE * arguments,
        c_size_t count,
        BOOK_MULTI_VALUE * returns
) {
        const unsigned char * bytes;
        c_size_t length;
        int64_t first = 1;
        int64_t last;

        if (
                book_std_string_arg(
                        runtime,
                        arguments,
                        count,
                        0,
                        &bytes,
                        &length,
                        "string.byte() requires a string"
                ) != NORMAL
        ) {
                return ABNORMAL;
        }

        last = first;

        if (count > 1) {
                double number;
                if (
                        book_std_number_arg(
                                runtime,
                                arguments,
                                count,
                                1,
                                &number,
                                "string.byte index must be a number"
                        ) != NORMAL
                        || book_std_integer(
                                runtime,
                                number,
                                &first,
                                "string.byte index must be an integer"
                        ) != NORMAL
                ) {
                        return ABNORMAL;
                }
                last = first;
        }

        if (count > 2) {
                double number;
                if (
                        book_std_number_arg(
                                runtime,
                                arguments,
                                count,
                                2,
                                &number,
                                "string.byte end index must be a number"
                        ) != NORMAL
                        || book_std_integer(
                                runtime,
                                number,
                                &last,
                                "string.byte end index must be an integer"
                        ) != NORMAL
                ) {
                        return ABNORMAL;
                }
        }

        first = book_std_pos_relative(first, length);
        last = book_std_pos_relative(last, length);

        if (first < 1) first = 1;
        if (last > (int64_t)length) last = (int64_t)length;

        book_std_multi_clear(returns);
        if (first > last || first > (int64_t)length) return NORMAL;

        if ((uint64_t)(last - first + 1) > BOOK_RUNTIME_MULTI_LIMIT) {
                return book_runtime_native_fail(
                        runtime,
                        "string.byte return limit exceeded"
                );
        }

        for (int64_t i = first; i <= last; i++) {
                returns->values[returns->count++] =
                        book_runtime_number((double)bytes[i - 1]);
        }

        return NORMAL;
}

static int book_std_string_char(
        BOOK_RUNTIME * runtime,
        const BOOK_VALUE * arguments,
        c_size_t count,
        BOOK_MULTI_VALUE * returns
) {
        unsigned char bytes[BOOK_RUNTIME_MULTI_LIMIT];
        BOOK_VALUE value;

        if (count > BOOK_RUNTIME_MULTI_LIMIT) {
                return book_runtime_native_fail(
                        runtime,
                        "string.char argument limit exceeded"
                );
        }

        for (c_size_t i = 0; i < count; i++) {
                double number;
                int64_t integer;

                if (
                        book_value_number(
                                &arguments[i],
                                &number
                        ) != NORMAL
                        || book_std_integer(
                                runtime,
                                number,
                                &integer,
                                "string.char arguments must be integers"
                        ) != NORMAL
                        || integer < 0
                        || integer > 255
                ) {
                        return book_runtime_native_fail(
                                runtime,
                                "string.char byte is outside 0..255"
                        );
                }

                bytes[i] = (unsigned char)integer;
        }

        if (
                book_runtime_make_string(
                        runtime,
                        bytes,
                        count,
                        &value
                ) != NORMAL
        ) {
                return ABNORMAL;
        }

        return book_std_return(returns, value);
}

static int book_std_string_sub(
        BOOK_RUNTIME * runtime,
        const BOOK_VALUE * arguments,
        c_size_t count,
        BOOK_MULTI_VALUE * returns
) {
        const unsigned char * bytes;
        c_size_t length;
        double number;
        int64_t first;
        int64_t last = -1;
        BOOK_VALUE value;

        if (
                book_std_string_arg(
                        runtime,
                        arguments,
                        count,
                        0,
                        &bytes,
                        &length,
                        "string.sub() requires a string"
                ) != NORMAL
                || book_std_number_arg(
                        runtime,
                        arguments,
                        count,
                        1,
                        &number,
                        "string.sub() requires a start index"
                ) != NORMAL
                || book_std_integer(
                        runtime,
                        number,
                        &first,
                        "string.sub start index must be an integer"
                ) != NORMAL
        ) {
                return ABNORMAL;
        }

        if (count > 2) {
                if (
                        book_std_number_arg(
                                runtime,
                                arguments,
                                count,
                                2,
                                &number,
                                "string.sub end index must be a number"
                        ) != NORMAL
                        || book_std_integer(
                                runtime,
                                number,
                                &last,
                                "string.sub end index must be an integer"
                        ) != NORMAL
                ) {
                        return ABNORMAL;
                }
        }

        first = book_std_pos_relative(first, length);
        last = book_std_pos_relative(last, length);

        if (first < 1) first = 1;
        if (last > (int64_t)length) last = (int64_t)length;

        if (first > last || first > (int64_t)length) {
                return book_runtime_make_string(
                        runtime,
                        NULL,
                        0,
                        &value
                ) == NORMAL
                        ? book_std_return(returns, value)
                        : ABNORMAL;
        }

        if (
                book_runtime_make_string(
                        runtime,
                        bytes + first - 1,
                        (c_size_t)(last - first + 1),
                        &value
                ) != NORMAL
        ) {
                return ABNORMAL;
        }

        return book_std_return(returns, value);
}

static int book_std_string_lower(
        BOOK_RUNTIME * runtime,
        const BOOK_VALUE * arguments,
        c_size_t count,
        BOOK_MULTI_VALUE * returns
) {
        const unsigned char * bytes;
        c_size_t length;
        BOOK_BUFFER buffer;
        BOOK_VALUE value;

        memset(&buffer, 0x00, sizeof(buffer));

        if (
                book_std_string_arg(
                        runtime,
                        arguments,
                        count,
                        0,
                        &bytes,
                        &length,
                        "string.lower() requires a string"
                ) != NORMAL
        ) {
                return ABNORMAL;
        }

        if (book_buffer_reserve(&buffer, length) != NORMAL) {
                return book_runtime_native_fail(
                        runtime,
                        "string.lower result is too large"
                );
        }

        for (c_size_t i = 0; i < length; i++) {
                buffer.data[buffer.length++] =
                        (unsigned char)tolower(bytes[i]);
        }

        if (book_std_make_string(runtime, &buffer, &value) != NORMAL) {
                book_buffer_free(&buffer);
                return ABNORMAL;
        }

        book_buffer_free(&buffer);
        return book_std_return(returns, value);
}

static int book_std_plain_find(
        const unsigned char * subject,
        c_size_t subject_length,
        const unsigned char * needle,
        c_size_t needle_length,
        c_size_t start,
        c_size_t * found
) {
        if (!subject || !needle || !found) return ISFALSE;
        if (needle_length == 0) {
                *found = start;
                return start <= subject_length ? ISTRUE : ISFALSE;
        }

        if (
                start > subject_length
                || needle_length > subject_length
        ) return ISFALSE;

        for (
                c_size_t i = start;
                i + needle_length <= subject_length;
                i++
        ) {
                if (memcmp(subject + i, needle, needle_length) == MATCH) {
                        *found = i;
                        return ISTRUE;
                }
        }

        return ISFALSE;
}

static c_size_t book_std_find_start(
        BOOK_RUNTIME * runtime,
        const BOOK_VALUE * arguments,
        c_size_t count,
        c_size_t length,
        int * status
) {
        int64_t start = 1;

        *status = NORMAL;

        if (count > 2) {
                double number;

                if (
                        book_value_number(
                                &arguments[2],
                                &number
                        ) != NORMAL
                        || book_std_integer(
                                runtime,
                                number,
                                &start,
                                "string search start must be an integer"
                        ) != NORMAL
                ) {
                        *status = ABNORMAL;
                        return 0;
                }
        }

        start = book_std_pos_relative(start, length);
        if (start < 1) start = 1;
        if (start > (int64_t)length + 1) return length + 1U;
        return (c_size_t)(start - 1);
}

static int book_std_string_find(
        BOOK_RUNTIME * runtime,
        const BOOK_VALUE * arguments,
        c_size_t count,
        BOOK_MULTI_VALUE * returns
) {
        const unsigned char * subject;
        const unsigned char * pattern;
        c_size_t subject_length;
        c_size_t pattern_length;
        c_size_t start;
        int status;
        int plain = ISFALSE;

        if (
                book_std_string_arg(
                        runtime, arguments, count, 0,
                        &subject, &subject_length,
                        "string.find() requires a subject string"
                ) != NORMAL
                || book_std_string_arg(
                        runtime, arguments, count, 1,
                        &pattern, &pattern_length,
                        "string.find() requires a pattern string"
                ) != NORMAL
        ) {
                return ABNORMAL;
        }

        start = book_std_find_start(
                runtime,
                arguments,
                count,
                subject_length,
                &status
        );
        if (status != NORMAL) return ABNORMAL;

        if (
                count > 3
                && book_runtime_truthy_value(&arguments[3]) == ISTRUE
        ) {
                plain = ISTRUE;
        }

        if (plain == ISTRUE) {
                c_size_t found;

                if (
                        book_std_plain_find(
                                subject,
                                subject_length,
                                pattern,
                                pattern_length,
                                start,
                                &found
                        ) != ISTRUE
                ) {
                        return book_std_return(
                                returns,
                                book_runtime_nil()
                        );
                }

                return book_std_return2(
                        returns,
                        book_runtime_number((double)(found + 1U)),
                        book_runtime_number(
                                (double)(found + pattern_length)
                        )
                );
        }

        {
                BOOK_PATTERN_MATCH match;

                if (
                        book_pattern_search(
                                subject,
                                subject_length,
                                pattern,
                                pattern_length,
                                start,
                                &match
                        ) != ISTRUE
                ) {
                        return book_std_return(
                                returns,
                                book_runtime_nil()
                        );
                }

                book_std_multi_clear(returns);
                returns->values[returns->count++] =
                        book_runtime_number((double)(match.start + 1U));
                returns->values[returns->count++] =
                        book_runtime_number((double)match.end);

                for (
                        c_size_t i = 0;
                        i < match.capture_count;
                        i++
                ) {
                        BOOK_VALUE capture;

                        if (
                                book_runtime_make_string(
                                        runtime,
                                        subject + match.captures[i].start,
                                        match.captures[i].end
                                                - match.captures[i].start,
                                        &capture
                                ) != NORMAL
                        ) {
                                return ABNORMAL;
                        }
                        returns->values[returns->count++] = capture;
                }

                return NORMAL;
        }
}

static int book_std_string_match(
        BOOK_RUNTIME * runtime,
        const BOOK_VALUE * arguments,
        c_size_t count,
        BOOK_MULTI_VALUE * returns
) {
        const unsigned char * subject;
        const unsigned char * pattern;
        c_size_t subject_length;
        c_size_t pattern_length;
        c_size_t start;
        int status;
        BOOK_PATTERN_MATCH match;

        if (
                book_std_string_arg(
                        runtime, arguments, count, 0,
                        &subject, &subject_length,
                        "string.match() requires a subject string"
                ) != NORMAL
                || book_std_string_arg(
                        runtime, arguments, count, 1,
                        &pattern, &pattern_length,
                        "string.match() requires a pattern string"
                ) != NORMAL
        ) {
                return ABNORMAL;
        }

        start = book_std_find_start(
                runtime,
                arguments,
                count,
                subject_length,
                &status
        );
        if (status != NORMAL) return ABNORMAL;

        if (
                book_pattern_search(
                        subject,
                        subject_length,
                        pattern,
                        pattern_length,
                        start,
                        &match
                ) != ISTRUE
        ) {
                return book_std_return(returns, book_runtime_nil());
        }

        return book_pattern_return_match(
                runtime,
                subject,
                &match,
                returns,
                ISTRUE
        );
}

static int book_std_gmatch_iterator(
        BOOK_RUNTIME * runtime,
        const BOOK_VALUE * arguments,
        c_size_t count,
        BOOK_MULTI_VALUE * returns
) {
        BOOK_VALUE subject_value;
        BOOK_VALUE pattern_value;
        BOOK_VALUE position_value;
        const unsigned char * subject;
        const unsigned char * pattern;
        c_size_t subject_length;
        c_size_t pattern_length;
        double position_number;
        int64_t position;
        BOOK_PATTERN_MATCH match;

        if (
                !runtime
                || !returns
                || count < 1
                || arguments[0].type != BOOK_VALUE_TABLE
        ) {
                return book_runtime_native_fail(
                        runtime,
                        "invalid gmatch iterator state"
                );
        }

        if (
                book_runtime_table_get_string_value(
                        runtime,
                        arguments[0],
                        "_subject",
                        &subject_value
                ) != NORMAL
                || book_runtime_table_get_string_value(
                        runtime,
                        arguments[0],
                        "_pattern",
                        &pattern_value
                ) != NORMAL
                || book_runtime_table_get_string_value(
                        runtime,
                        arguments[0],
                        "_position",
                        &position_value
                ) != NORMAL
                || book_value_string(
                        &subject_value,
                        &subject,
                        &subject_length
                ) != NORMAL
                || book_value_string(
                        &pattern_value,
                        &pattern,
                        &pattern_length
                ) != NORMAL
                || book_value_number(
                        &position_value,
                        &position_number
                ) != NORMAL
                || book_std_integer(
                        runtime,
                        position_number,
                        &position,
                        "invalid gmatch iterator position"
                ) != NORMAL
        ) {
                return ABNORMAL;
        }

        if (
                position < 0
                || (uint64_t)position > subject_length
                || book_pattern_search(
                        subject,
                        subject_length,
                        pattern,
                        pattern_length,
                        (c_size_t)position,
                        &match
                ) != ISTRUE
        ) {
                book_std_multi_clear(returns);
                return NORMAL;
        }

        if (
                book_runtime_table_set_string(
                        runtime,
                        arguments[0],
                        "_position",
                        book_runtime_number(
                                (double)(
                                        match.end > match.start
                                                ? match.end
                                                : match.end + 1U
                                )
                        )
                ) != NORMAL
        ) {
                return ABNORMAL;
        }

        return book_pattern_return_match(
                runtime,
                subject,
                &match,
                returns,
                ISTRUE
        );
}

static int book_std_string_gmatch(
        BOOK_RUNTIME * runtime,
        const BOOK_VALUE * arguments,
        c_size_t count,
        BOOK_MULTI_VALUE * returns
) {
        const unsigned char * unused;
        c_size_t unused_length;
        BOOK_VALUE state;
        BOOK_VALUE iterator;

        if (
                book_std_string_arg(
                        runtime, arguments, count, 0,
                        &unused, &unused_length,
                        "string.gmatch() requires a subject string"
                ) != NORMAL
                || book_std_string_arg(
                        runtime, arguments, count, 1,
                        &unused, &unused_length,
                        "string.gmatch() requires a pattern string"
                ) != NORMAL
        ) {
                return ABNORMAL;
        }

        if (
                book_runtime_make_table(runtime, &state) != NORMAL
                || book_runtime_table_set_string(
                        runtime, state, "_subject", arguments[0]
                ) != NORMAL
                || book_runtime_table_set_string(
                        runtime, state, "_pattern", arguments[1]
                ) != NORMAL
                || book_runtime_table_set_string(
                        runtime, state, "_position",
                        book_runtime_number(0)
                ) != NORMAL
                || book_runtime_make_native(
                        runtime,
                        "string.gmatch.iterator",
                        book_std_gmatch_iterator,
                        &iterator
                ) != NORMAL
        ) {
                return ABNORMAL;
        }

        book_std_multi_clear(returns);
        returns->values[0] = iterator;
        returns->values[1] = state;
        returns->values[2] = book_runtime_nil();
        returns->count = 3;
        return NORMAL;
}

static int book_std_append_replacement_string(
        BOOK_RUNTIME * runtime,
        BOOK_BUFFER * buffer,
        const unsigned char * replacement,
        c_size_t replacement_length,
        const unsigned char * subject,
        const BOOK_PATTERN_MATCH * match
) {
        for (c_size_t i = 0; i < replacement_length; i++) {
                if (replacement[i] != '%') {
                        if (
                                book_buffer_byte(
                                        buffer,
                                        replacement[i]
                                ) != NORMAL
                        ) return ABNORMAL;
                        continue;
                }

                if (i + 1U >= replacement_length) {
                        return book_runtime_native_fail(
                                runtime,
                                "invalid gsub replacement escape"
                        );
                }

                i++;
                if (replacement[i] == '%') {
                        if (book_buffer_byte(buffer, '%') != NORMAL) {
                                return ABNORMAL;
                        }
                        continue;
                }

                if (replacement[i] >= '0' && replacement[i] <= '9') {
                        unsigned int index =
                                (unsigned int)(replacement[i] - '0');
                        c_size_t start;
                        c_size_t end;

                        if (index == 0) {
                                start = match->start;
                                end = match->end;
                        } else if (index <= match->capture_count) {
                                start = match->captures[index - 1U].start;
                                end = match->captures[index - 1U].end;
                        } else {
                                return book_runtime_native_fail(
                                        runtime,
                                        "gsub replacement capture is unavailable"
                                );
                        }

                        if (
                                book_buffer_append(
                                        buffer,
                                        subject + start,
                                        end - start
                                ) != NORMAL
                        ) {
                                return ABNORMAL;
                        }
                        continue;
                }

                return book_runtime_native_fail(
                        runtime,
                        "unsupported gsub replacement escape"
                );
        }

        return NORMAL;
}

static int book_std_string_gsub(
        BOOK_RUNTIME * runtime,
        const BOOK_VALUE * arguments,
        c_size_t count,
        BOOK_MULTI_VALUE * returns
) {
        const unsigned char * subject;
        const unsigned char * pattern;
        c_size_t subject_length;
        c_size_t pattern_length;
        c_size_t maximum = SIZE_MAX;
        c_size_t position = 0;
        c_size_t replacements = 0;
        BOOK_BUFFER output;

        memset(&output, 0x00, sizeof(output));

        if (
                book_std_string_arg(
                        runtime, arguments, count, 0,
                        &subject, &subject_length,
                        "string.gsub() requires a subject string"
                ) != NORMAL
                || book_std_string_arg(
                        runtime, arguments, count, 1,
                        &pattern, &pattern_length,
                        "string.gsub() requires a pattern string"
                ) != NORMAL
                || count < 3
        ) {
                return ABNORMAL;
        }

        if (count > 3) {
                double number;
                int64_t integer;

                if (
                        book_value_number(
                                &arguments[3],
                                &number
                        ) != NORMAL
                        || book_std_integer(
                                runtime,
                                number,
                                &integer,
                                "gsub replacement count must be an integer"
                        ) != NORMAL
                        || integer < 0
                ) {
                        return book_runtime_native_fail(
                                runtime,
                                "gsub replacement count must be non-negative"
                        );
                }

                maximum = (c_size_t)integer;
        }

        while (position <= subject_length && replacements < maximum) {
                BOOK_PATTERN_MATCH match;

                if (
                        book_pattern_search(
                                subject,
                                subject_length,
                                pattern,
                                pattern_length,
                                position,
                                &match
                        ) != ISTRUE
                ) {
                        break;
                }

                if (
                        book_buffer_append(
                                &output,
                                subject + position,
                                match.start - position
                        ) != NORMAL
                ) {
                        book_buffer_free(&output);
                        return book_runtime_native_fail(
                                runtime,
                                "gsub result is too large"
                        );
                }

                if (arguments[2].type == BOOK_VALUE_STRING) {
                        const unsigned char * replacement;
                        c_size_t replacement_length;

                        if (
                                book_value_string(
                                        &arguments[2],
                                        &replacement,
                                        &replacement_length
                                ) != NORMAL
                                || book_std_append_replacement_string(
                                        runtime,
                                        &output,
                                        replacement,
                                        replacement_length,
                                        subject,
                                        &match
                                ) != NORMAL
                        ) {
                                book_buffer_free(&output);
                                return ABNORMAL;
                        }
                } else if (
                        arguments[2].type == BOOK_VALUE_FUNCTION
                        || arguments[2].type == BOOK_VALUE_NATIVE_FUNCTION
                ) {
                        BOOK_MULTI_VALUE callback_arguments;
                        BOOK_MULTI_VALUE callback_returns;
                        BOOK_VALUE replacement_value;
                        BOOK_VALUE replacement_string;
                        const unsigned char * replacement;
                        c_size_t replacement_length;

                        book_std_multi_clear(&callback_arguments);
                        book_std_multi_clear(&callback_returns);

                        if (
                                book_pattern_return_match(
                                        runtime,
                                        subject,
                                        &match,
                                        &callback_arguments,
                                        ISTRUE
                                ) != NORMAL
                                || book_runtime_call_native_value(
                                        runtime,
                                        arguments[2],
                                        callback_arguments.values,
                                        callback_arguments.count,
                                        &callback_returns
                                ) != NORMAL
                        ) {
                                book_buffer_free(&output);
                                return ABNORMAL;
                        }

                        replacement_value = callback_returns.count > 0
                                ? callback_returns.values[0]
                                : book_runtime_nil();

                        if (
                                replacement_value.type == BOOK_VALUE_NIL
                                || (
                                        replacement_value.type
                                                == BOOK_VALUE_BOOLEAN
                                        && replacement_value.as.boolean
                                                != ISTRUE
                                )
                        ) {
                                if (
                                        book_buffer_append(
                                                &output,
                                                subject + match.start,
                                                match.end - match.start
                                        ) != NORMAL
                                ) {
                                        book_buffer_free(&output);
                                        return ABNORMAL;
                                }
                        } else {
                                if (
                                        book_std_value_to_string(
                                                runtime,
                                                replacement_value,
                                                &replacement_string
                                        ) != NORMAL
                                        || book_value_string(
                                                &replacement_string,
                                                &replacement,
                                                &replacement_length
                                        ) != NORMAL
                                        || book_buffer_append(
                                                &output,
                                                replacement,
                                                replacement_length
                                        ) != NORMAL
                                ) {
                                        book_buffer_free(&output);
                                        return ABNORMAL;
                                }
                        }
                } else {
                        book_buffer_free(&output);
                        return book_runtime_native_fail(
                                runtime,
                                "gsub replacement must be a string or function"
                        );
                }

                replacements++;

                if (match.end > match.start) {
                        position = match.end;
                } else {
                        if (match.end < subject_length) {
                                if (
                                        book_buffer_byte(
                                                &output,
                                                subject[match.end]
                                        ) != NORMAL
                                ) {
                                        book_buffer_free(&output);
                                        return ABNORMAL;
                                }
                        }
                        position = match.end + 1U;
                }
        }

        if (position <= subject_length) {
                if (
                        book_buffer_append(
                                &output,
                                subject + position,
                                subject_length - position
                        ) != NORMAL
                ) {
                        book_buffer_free(&output);
                        return ABNORMAL;
                }
        }

        {
                BOOK_VALUE value;

                if (
                        book_std_make_string(
                                runtime,
                                &output,
                                &value
                        ) != NORMAL
                ) {
                        book_buffer_free(&output);
                        return ABNORMAL;
                }

                book_buffer_free(&output);
                return book_std_return2(
                        returns,
                        value,
                        book_runtime_number((double)replacements)
                );
        }
}

static int book_std_format_append_number(
        BOOK_RUNTIME * runtime,
        BOOK_BUFFER * output,
        BOOK_VALUE value,
        char conversion,
        int width,
        int zero_pad
) {
        char format[32];
        char rendered[256];
        int written;
        double number;
        int64_t integer;

        if (
                book_value_number(&value, &number) != NORMAL
        ) {
                return book_runtime_native_fail(
                        runtime,
                        "string.format numeric conversion requires a number"
                );
        }

        if (conversion == 'd' || conversion == 'i'
                || conversion == 'u' || conversion == 'x'
                || conversion == 'X' || conversion == 'o'
                || conversion == 'c') {
                if (
                        book_std_integer(
                                runtime,
                                number,
                                &integer,
                                "string.format integer conversion requires an integer"
                        ) != NORMAL
                ) return ABNORMAL;

                if (zero_pad && width > 0) {
                        snprintf(
                                format,
                                sizeof(format),
                                "%%0%dll%c",
                                width,
                                conversion
                        );
                } else if (width > 0) {
                        snprintf(
                                format,
                                sizeof(format),
                                "%%%dll%c",
                                width,
                                conversion
                        );
                } else {
                        snprintf(
                                format,
                                sizeof(format),
                                "%%ll%c",
                                conversion
                        );
                }

                if (conversion == 'c') {
                        if (integer < 0 || integer > 255) {
                                return book_runtime_native_fail(
                                        runtime,
                                        "string.format %c is outside byte range"
                                );
                        }
                        rendered[0] = (char)integer;
                        written = 1;
                } else if (conversion == 'u' || conversion == 'x'
                        || conversion == 'X' || conversion == 'o') {
                        written = snprintf(
                                rendered,
                                sizeof(rendered),
                                format,
                                (unsigned long long)integer
                        );
                } else {
                        written = snprintf(
                                rendered,
                                sizeof(rendered),
                                format,
                                (long long)integer
                        );
                }
        } else {
                snprintf(
                        format,
                        sizeof(format),
                        width > 0 ? "%%%d%c" : "%%%c",
                        width,
                        conversion
                );
                written = snprintf(
                        rendered,
                        sizeof(rendered),
                        format,
                        number
                );
        }

        if (written < 0 || written >= (int)sizeof(rendered)) {
                return book_runtime_native_fail(
                        runtime,
                        "string.format output is too large"
                );
        }

        return book_buffer_append(
                output,
                (const unsigned char*)rendered,
                (c_size_t)written
        );
}

static int book_std_string_format(
        BOOK_RUNTIME * runtime,
        const BOOK_VALUE * arguments,
        c_size_t count,
        BOOK_MULTI_VALUE * returns
) {
        const unsigned char * format;
        c_size_t format_length;
        c_size_t argument = 1;
        BOOK_BUFFER output;
        BOOK_VALUE result;

        memset(&output, 0x00, sizeof(output));

        if (
                book_std_string_arg(
                        runtime, arguments, count, 0,
                        &format, &format_length,
                        "string.format() requires a format string"
                ) != NORMAL
        ) {
                return ABNORMAL;
        }

        for (c_size_t i = 0; i < format_length; i++) {
                if (format[i] != '%') {
                        if (book_buffer_byte(&output, format[i]) != NORMAL) {
                                book_buffer_free(&output);
                                return ABNORMAL;
                        }
                        continue;
                }

                if (i + 1U >= format_length) {
                        book_buffer_free(&output);
                        return book_runtime_native_fail(
                                runtime,
                                "incomplete string.format conversion"
                        );
                }

                i++;
                if (format[i] == '%') {
                        if (book_buffer_byte(&output, '%') != NORMAL) {
                                book_buffer_free(&output);
                                return ABNORMAL;
                        }
                        continue;
                }

                {
                        int zero_pad = ISFALSE;
                        int width = 0;
                        unsigned char conversion;

                        if (format[i] == '0') {
                                zero_pad = ISTRUE;
                                i++;
                        }

                        while (
                                i < format_length
                                && isdigit(format[i])
                        ) {
                                if (width > 1000) {
                                        book_buffer_free(&output);
                                        return book_runtime_native_fail(
                                                runtime,
                                                "string.format width is too large"
                                        );
                                }
                                width = width * 10 + (format[i] - '0');
                                i++;
                        }

                        if (i >= format_length || argument >= count) {
                                book_buffer_free(&output);
                                return book_runtime_native_fail(
                                        runtime,
                                        "string.format has missing arguments"
                                );
                        }

                        conversion = format[i];

                        if (conversion == 's') {
                                BOOK_VALUE string_value;
                                const unsigned char * bytes;
                                c_size_t length;
                                c_size_t padding = 0;

                                if (
                                        book_std_value_to_string(
                                                runtime,
                                                arguments[argument++],
                                                &string_value
                                        ) != NORMAL
                                        || book_value_string(
                                                &string_value,
                                                &bytes,
                                                &length
                                        ) != NORMAL
                                ) {
                                        book_buffer_free(&output);
                                        return ABNORMAL;
                                }

                                if (
                                        width > 0
                                        && (c_size_t)width > length
                                ) {
                                        padding = (c_size_t)width - length;
                                }

                                for (c_size_t p = 0; p < padding; p++) {
                                        if (
                                                book_buffer_byte(
                                                        &output,
                                                        ' '
                                                ) != NORMAL
                                        ) {
                                                book_buffer_free(&output);
                                                return ABNORMAL;
                                        }
                                }

                                if (
                                        book_buffer_append(
                                                &output,
                                                bytes,
                                                length
                                        ) != NORMAL
                                ) {
                                        book_buffer_free(&output);
                                        return ABNORMAL;
                                }
                        } else if (
                                conversion == 'd'
                                || conversion == 'i'
                                || conversion == 'u'
                                || conversion == 'x'
                                || conversion == 'X'
                                || conversion == 'o'
                                || conversion == 'c'
                                || conversion == 'f'
                                || conversion == 'F'
                                || conversion == 'e'
                                || conversion == 'E'
                                || conversion == 'g'
                                || conversion == 'G'
                        ) {
                                if (
                                        book_std_format_append_number(
                                                runtime,
                                                &output,
                                                arguments[argument++],
                                                (char)conversion,
                                                width,
                                                zero_pad
                                        ) != NORMAL
                                ) {
                                        book_buffer_free(&output);
                                        return ABNORMAL;
                                }
                        } else {
                                book_buffer_free(&output);
                                return book_runtime_native_fail(
                                        runtime,
                                        "unsupported string.format conversion"
                                );
                        }
                }
        }

        if (book_std_make_string(runtime, &output, &result) != NORMAL) {
                book_buffer_free(&output);
                return ABNORMAL;
        }

        book_buffer_free(&output);
        return book_std_return(returns, result);
}

// -----------------------------------------------------------------------------
// Table library
// -----------------------------------------------------------------------------

static int book_std_table_concat(
        BOOK_RUNTIME * runtime,
        const BOOK_VALUE * arguments,
        c_size_t count,
        BOOK_MULTI_VALUE * returns
) {
        const unsigned char * separator = (const unsigned char*)"";
        c_size_t separator_length = 0;
        c_size_t length;
        int64_t first = 1;
        int64_t last;
        BOOK_BUFFER output;
        BOOK_VALUE result;

        memset(&output, 0x00, sizeof(output));

        if (
                count < 1
                || arguments[0].type != BOOK_VALUE_TABLE
        ) {
                return book_runtime_native_fail(
                        runtime,
                        "table.concat() requires a table"
                );
        }

        length = book_runtime_table_length_value(&arguments[0]);
        last = (int64_t)length;

        if (count > 1 && arguments[1].type != BOOK_VALUE_NIL) {
                if (
                        book_value_string(
                                &arguments[1],
                                &separator,
                                &separator_length
                        ) != NORMAL
                ) {
                        return book_runtime_native_fail(
                                runtime,
                                "table.concat separator must be a string"
                        );
                }
        }

        if (count > 2) {
                double number;
                if (
                        book_value_number(&arguments[2], &number) != NORMAL
                        || book_std_integer(
                                runtime, number, &first,
                                "table.concat start must be an integer"
                        ) != NORMAL
                ) return ABNORMAL;
        }

        if (count > 3) {
                double number;
                if (
                        book_value_number(&arguments[3], &number) != NORMAL
                        || book_std_integer(
                                runtime, number, &last,
                                "table.concat end must be an integer"
                        ) != NORMAL
                ) return ABNORMAL;
        }

        for (int64_t i = first; i <= last; i++) {
                BOOK_VALUE key = book_runtime_number((double)i);
                BOOK_VALUE value;
                BOOK_VALUE string_value;
                const unsigned char * bytes;
                c_size_t value_length;

                if (
                        book_runtime_table_get_value(
                                runtime,
                                arguments[0],
                                &key,
                                &value
                        ) != NORMAL
                        || value.type == BOOK_VALUE_NIL
                ) {
                        book_buffer_free(&output);
                        return book_runtime_native_fail(
                                runtime,
                                "invalid value in table.concat"
                        );
                }

                if (
                        book_std_value_to_string(
                                runtime,
                                value,
                                &string_value
                        ) != NORMAL
                        || book_value_string(
                                &string_value,
                                &bytes,
                                &value_length
                        ) != NORMAL
                ) {
                        book_buffer_free(&output);
                        return ABNORMAL;
                }

                if (
                        i > first
                        && book_buffer_append(
                                &output,
                                separator,
                                separator_length
                        ) != NORMAL
                ) {
                        book_buffer_free(&output);
                        return ABNORMAL;
                }

                if (
                        book_buffer_append(
                                &output,
                                bytes,
                                value_length
                        ) != NORMAL
                ) {
                        book_buffer_free(&output);
                        return ABNORMAL;
                }
        }

        if (book_std_make_string(runtime, &output, &result) != NORMAL) {
                book_buffer_free(&output);
                return ABNORMAL;
        }

        book_buffer_free(&output);
        return book_std_return(returns, result);
}

static int book_std_table_insert(
        BOOK_RUNTIME * runtime,
        const BOOK_VALUE * arguments,
        c_size_t count,
        BOOK_MULTI_VALUE * returns
) {
        c_size_t length;
        int64_t position;
        BOOK_VALUE value;

        if (
                !runtime
                || !returns
                || count < 2
                || arguments[0].type != BOOK_VALUE_TABLE
        ) {
                return book_runtime_native_fail(
                        runtime,
                        "table.insert() requires a table and value"
                );
        }

        length = book_runtime_table_length_value(&arguments[0]);

        if (count == 2) {
                position = (int64_t)length + 1;
                value = arguments[1];
        } else {
                double number;

                if (
                        book_value_number(&arguments[1], &number) != NORMAL
                        || book_std_integer(
                                runtime, number, &position,
                                "table.insert position must be an integer"
                        ) != NORMAL
                ) {
                        return ABNORMAL;
                }

                value = arguments[2];
        }

        if (position < 1 || position > (int64_t)length + 1) {
                return book_runtime_native_fail(
                        runtime,
                        "table.insert position is out of bounds"
                );
        }

        for (
                int64_t i = (int64_t)length;
                i >= position;
                i--
        ) {
                BOOK_VALUE from_key = book_runtime_number((double)i);
                BOOK_VALUE to_key = book_runtime_number((double)(i + 1));
                BOOK_VALUE current;

                if (
                        book_runtime_table_get_value(
                                runtime,
                                arguments[0],
                                &from_key,
                                &current
                        ) != NORMAL
                        || book_runtime_table_set_value(
                                runtime,
                                arguments[0],
                                to_key,
                                current
                        ) != NORMAL
                ) {
                        return ABNORMAL;
                }
        }

        if (
                book_runtime_table_set_value(
                        runtime,
                        arguments[0],
                        book_runtime_number((double)position),
                        value
                ) != NORMAL
        ) {
                return ABNORMAL;
        }

        book_std_multi_clear(returns);
        return NORMAL;
}

static int book_std_table_remove(
        BOOK_RUNTIME * runtime,
        const BOOK_VALUE * arguments,
        c_size_t count,
        BOOK_MULTI_VALUE * returns
) {
        c_size_t length;
        int64_t position;
        BOOK_VALUE removed = book_runtime_nil();

        if (
                !runtime
                || !returns
                || count < 1
                || arguments[0].type != BOOK_VALUE_TABLE
        ) {
                return book_runtime_native_fail(
                        runtime,
                        "table.remove() requires a table"
                );
        }

        length = book_runtime_table_length_value(&arguments[0]);
        position = (int64_t)length;

        if (count > 1) {
                double number;

                if (
                        book_value_number(&arguments[1], &number) != NORMAL
                        || book_std_integer(
                                runtime, number, &position,
                                "table.remove position must be an integer"
                        ) != NORMAL
                ) return ABNORMAL;
        }

        if (
                length == 0
                || position < 1
                || position > (int64_t)length
        ) {
                return book_std_return(returns, book_runtime_nil());
        }

        {
                BOOK_VALUE key = book_runtime_number((double)position);

                if (
                        book_runtime_table_get_value(
                                runtime,
                                arguments[0],
                                &key,
                                &removed
                        ) != NORMAL
                ) return ABNORMAL;
        }

        for (
                int64_t i = position;
                i < (int64_t)length;
                i++
        ) {
                BOOK_VALUE from_key = book_runtime_number((double)(i + 1));
                BOOK_VALUE to_key = book_runtime_number((double)i);
                BOOK_VALUE current;

                if (
                        book_runtime_table_get_value(
                                runtime,
                                arguments[0],
                                &from_key,
                                &current
                        ) != NORMAL
                        || book_runtime_table_set_value(
                                runtime,
                                arguments[0],
                                to_key,
                                current
                        ) != NORMAL
                ) {
                        return ABNORMAL;
                }
        }

        if (
                book_runtime_table_set_value(
                        runtime,
                        arguments[0],
                        book_runtime_number((double)length),
                        book_runtime_nil()
                ) != NORMAL
        ) {
                return ABNORMAL;
        }

        return book_std_return(returns, removed);
}

// -----------------------------------------------------------------------------
// Math library
// -----------------------------------------------------------------------------

static double book_std_floor_value(double number) {
        if (
                number <= -9223372036854775808.0
                || number >= 9223372036854775808.0
        ) return number;

        {
                int64_t integer = (int64_t)number;
                double result = (double)integer;
                if (result > number) result -= 1.0;
                return result;
        }
}

static double book_std_ceil_value(double number) {
        if (
                number <= -9223372036854775808.0
                || number >= 9223372036854775808.0
        ) return number;

        {
                int64_t integer = (int64_t)number;
                double result = (double)integer;
                if (result < number) result += 1.0;
                return result;
        }
}

static int book_std_math_abs(
        BOOK_RUNTIME * runtime,
        const BOOK_VALUE * arguments,
        c_size_t count,
        BOOK_MULTI_VALUE * returns
) {
        double number;

        if (
                book_std_number_arg(
                        runtime, arguments, count, 0, &number,
                        "math.abs() requires a number"
                ) != NORMAL
        ) return ABNORMAL;

        return book_std_return(
                returns,
                book_runtime_number(number < 0.0 ? -number : number)
        );
}

static int book_std_math_floor(
        BOOK_RUNTIME * runtime,
        const BOOK_VALUE * arguments,
        c_size_t count,
        BOOK_MULTI_VALUE * returns
) {
        double number;

        if (
                book_std_number_arg(
                        runtime, arguments, count, 0, &number,
                        "math.floor() requires a number"
                ) != NORMAL
        ) return ABNORMAL;

        return book_std_return(
                returns,
                book_runtime_number(book_std_floor_value(number))
        );
}

static int book_std_math_ceil(
        BOOK_RUNTIME * runtime,
        const BOOK_VALUE * arguments,
        c_size_t count,
        BOOK_MULTI_VALUE * returns
) {
        double number;

        if (
                book_std_number_arg(
                        runtime, arguments, count, 0, &number,
                        "math.ceil() requires a number"
                ) != NORMAL
        ) return ABNORMAL;

        return book_std_return(
                returns,
                book_runtime_number(book_std_ceil_value(number))
        );
}

static int book_std_math_extreme(
        BOOK_RUNTIME * runtime,
        const BOOK_VALUE * arguments,
        c_size_t count,
        BOOK_MULTI_VALUE * returns,
        int8_t maximum
) {
        double value;

        if (
                count == 0
                || book_value_number(&arguments[0], &value) != NORMAL
        ) {
                return book_runtime_native_fail(
                        runtime,
                        maximum == ISTRUE
                                ? "math.max() requires numbers"
                                : "math.min() requires numbers"
                );
        }

        for (c_size_t i = 1; i < count; i++) {
                double candidate;

                if (
                        book_value_number(
                                &arguments[i],
                                &candidate
                        ) != NORMAL
                ) {
                        return book_runtime_native_fail(
                                runtime,
                                "math min/max requires numbers"
                        );
                }

                if (
                        (maximum == ISTRUE && candidate > value)
                        || (maximum != ISTRUE && candidate < value)
                ) {
                        value = candidate;
                }
        }

        return book_std_return(
                returns,
                book_runtime_number(value)
        );
}

static int book_std_math_max(
        BOOK_RUNTIME * runtime,
        const BOOK_VALUE * arguments,
        c_size_t count,
        BOOK_MULTI_VALUE * returns
) {
        return book_std_math_extreme(
                runtime, arguments, count, returns, ISTRUE
        );
}

static int book_std_math_min(
        BOOK_RUNTIME * runtime,
        const BOOK_VALUE * arguments,
        c_size_t count,
        BOOK_MULTI_VALUE * returns
) {
        return book_std_math_extreme(
                runtime, arguments, count, returns, ISFALSE
        );
}

static int book_std_math_random(
        BOOK_RUNTIME * runtime,
        const BOOK_VALUE * arguments,
        c_size_t count,
        BOOK_MULTI_VALUE * returns
) {
        uint64_t random = book_runtime_random_u64(runtime);

        if (count == 0) {
                double value = (double)(random >> 11)
                        * (1.0 / 9007199254740992.0);

                return book_std_return(
                        returns,
                        book_runtime_number(value)
                );
        }

        {
                int64_t lower = 1;
                int64_t upper;
                double number;
                uint64_t span;
                uint64_t limit;

                if (count == 1) {
                        if (
                                book_value_number(
                                        &arguments[0],
                                        &number
                                ) != NORMAL
                                || book_std_integer(
                                        runtime, number, &upper,
                                        "math.random bound must be an integer"
                                ) != NORMAL
                        ) return ABNORMAL;
                } else {
                        if (
                                book_value_number(
                                        &arguments[0],
                                        &number
                                ) != NORMAL
                                || book_std_integer(
                                        runtime, number, &lower,
                                        "math.random lower bound must be an integer"
                                ) != NORMAL
                                || book_value_number(
                                        &arguments[1],
                                        &number
                                ) != NORMAL
                                || book_std_integer(
                                        runtime, number, &upper,
                                        "math.random upper bound must be an integer"
                                ) != NORMAL
                        ) return ABNORMAL;
                }

                if (lower > upper) {
                        return book_runtime_native_fail(
                                runtime,
                                "math.random interval is empty"
                        );
                }

                span = (uint64_t)upper - (uint64_t)lower + 1U;
                if (span == 0) {
                        return book_runtime_native_fail(
                                runtime,
                                "math.random interval is too large"
                        );
                }

                limit = UINT64_MAX - (UINT64_MAX % span);
                while (random >= limit) {
                        random = book_runtime_random_u64(runtime);
                }

                {
                        __int128 sampled = (__int128)lower
                                + (__int128)(random % span);

                        return book_std_return(
                                returns,
                                book_runtime_number(
                                        (double)(int64_t)sampled
                                )
                        );
                }
        }
}

// -----------------------------------------------------------------------------
// Library installation
// -----------------------------------------------------------------------------

static int book_std_table_function(
        BOOK_RUNTIME * runtime,
        BOOK_VALUE table,
        const char * name,
        book_native_callback_t callback
) {
        BOOK_VALUE function;

        if (
                book_runtime_make_native(
                        runtime,
                        name,
                        callback,
                        &function
                ) != NORMAL
        ) return ABNORMAL;

        return book_runtime_table_set_string(
                runtime,
                table,
                name,
                function
        );
}

int book_stdlib_install(BOOK_RUNTIME * runtime) {
        BOOK_VALUE string_library;
        BOOK_VALUE table_library;
        BOOK_VALUE math_library;

        if (!runtime) return ABNORMAL;

        if (
                book_runtime_register_global(
                        runtime, "assert", book_std_assert
                ) != NORMAL
                || book_runtime_register_global(
                        runtime, "error", book_std_error
                ) != NORMAL
                || book_runtime_register_global(
                        runtime, "type", book_std_type
                ) != NORMAL
                || book_runtime_register_global(
                        runtime, "tostring", book_std_tostring
                ) != NORMAL
                || book_runtime_register_global(
                        runtime, "tonumber", book_std_tonumber
                ) != NORMAL
                || book_runtime_register_global(
                        runtime, "pairs", book_std_pairs
                ) != NORMAL
                || book_runtime_register_global(
                        runtime, "ipairs", book_std_ipairs
                ) != NORMAL
        ) {
                return ABNORMAL;
        }

        if (
                book_runtime_make_table(runtime, &string_library) != NORMAL
                || book_std_table_function(
                        runtime, string_library,
                        "byte", book_std_string_byte
                ) != NORMAL
                || book_std_table_function(
                        runtime, string_library,
                        "char", book_std_string_char
                ) != NORMAL
                || book_std_table_function(
                        runtime, string_library,
                        "find", book_std_string_find
                ) != NORMAL
                || book_std_table_function(
                        runtime, string_library,
                        "format", book_std_string_format
                ) != NORMAL
                || book_std_table_function(
                        runtime, string_library,
                        "gmatch", book_std_string_gmatch
                ) != NORMAL
                || book_std_table_function(
                        runtime, string_library,
                        "gsub", book_std_string_gsub
                ) != NORMAL
                || book_std_table_function(
                        runtime, string_library,
                        "lower", book_std_string_lower
                ) != NORMAL
                || book_std_table_function(
                        runtime, string_library,
                        "match", book_std_string_match
                ) != NORMAL
                || book_std_table_function(
                        runtime, string_library,
                        "sub", book_std_string_sub
                ) != NORMAL
                || book_runtime_set_global(
                        runtime, "string", string_library
                ) != NORMAL
        ) {
                return ABNORMAL;
        }

        if (
                book_runtime_make_table(runtime, &table_library) != NORMAL
                || book_std_table_function(
                        runtime, table_library,
                        "concat", book_std_table_concat
                ) != NORMAL
                || book_std_table_function(
                        runtime, table_library,
                        "insert", book_std_table_insert
                ) != NORMAL
                || book_std_table_function(
                        runtime, table_library,
                        "remove", book_std_table_remove
                ) != NORMAL
                || book_runtime_set_global(
                        runtime, "table", table_library
                ) != NORMAL
        ) {
                return ABNORMAL;
        }

        if (
                book_runtime_make_table(runtime, &math_library) != NORMAL
                || book_std_table_function(
                        runtime, math_library,
                        "abs", book_std_math_abs
                ) != NORMAL
                || book_std_table_function(
                        runtime, math_library,
                        "ceil", book_std_math_ceil
                ) != NORMAL
                || book_std_table_function(
                        runtime, math_library,
                        "floor", book_std_math_floor
                ) != NORMAL
                || book_std_table_function(
                        runtime, math_library,
                        "max", book_std_math_max
                ) != NORMAL
                || book_std_table_function(
                        runtime, math_library,
                        "min", book_std_math_min
                ) != NORMAL
                || book_std_table_function(
                        runtime, math_library,
                        "random", book_std_math_random
                ) != NORMAL
                || book_runtime_set_global(
                        runtime, "math", math_library
                ) != NORMAL
        ) {
                return ABNORMAL;
        }

        return NORMAL;
}
