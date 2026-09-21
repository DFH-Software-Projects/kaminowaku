// Copyright 2026 Jamison A. Drapeau
#include "book_output.h"
#include <stdlib.h>
#include <string.h>

// @@ Semantic color token parser. ANSI never enters persisted/staged data.
int book_output_parse_color(const char * color, book_output_color_t * parsed) {
        if (!parsed) return ABNORMAL;

        if (!color || strcmp(color, "default") == MATCH) {
                *parsed = BOOK_OUTPUT_COLOR_DEFAULT;
                return NORMAL;
        }
        if (strcmp(color, "green") == MATCH) {
                *parsed = BOOK_OUTPUT_COLOR_GREEN;
                return NORMAL;
        }
        if (strcmp(color, "red") == MATCH) {
                *parsed = BOOK_OUTPUT_COLOR_RED;
                return NORMAL;
        }
        if (strcmp(color, "yellow") == MATCH) {
                *parsed = BOOK_OUTPUT_COLOR_YELLOW;
                return NORMAL;
        }
        if (strcmp(color, "grey") == MATCH) {
                *parsed = BOOK_OUTPUT_COLOR_GREY;
                return NORMAL;
        }

        return ABNORMAL;
}

const char * book_output_color_name(book_output_color_t color) {
        switch (color) {
                case BOOK_OUTPUT_COLOR_GREEN:  return "green";
                case BOOK_OUTPUT_COLOR_RED:    return "red";
                case BOOK_OUTPUT_COLOR_YELLOW: return "yellow";
                case BOOK_OUTPUT_COLOR_GREY:   return "grey";
                case BOOK_OUTPUT_COLOR_DEFAULT:
                default:                       return "default";
        }
}

static int book_output_key_valid(const char * key, c_size_t length) {
        if (!key || length == 0 || length >= BOOK_OUTPUT_KEY_BLOCK) return ISFALSE;

        for (c_size_t i = 0; i < length; i++) {
                unsigned char c = (unsigned char)key[i];

                // Keep staged keys inside the durable .out grammar. Spaces and
                // path/escape separators are intentionally excluded so a key
                // accepted by emit() cannot later fail the atomic commit path.
                if (
                        c < 0x21
                        || c == 0x7f
                        || c == ':'
                        || c == '\\'
                ) return ISFALSE;
        }

        return ISTRUE;
}

static int book_output_value_valid(const char * value, c_size_t length) {
        if (!value || length >= BOOK_OUTPUT_VALUE_BLOCK) return ISFALSE;

        for (c_size_t i = 0; i < length; i++) {
                unsigned char c = (unsigned char)value[i];

                if (c == 0x00) return ISFALSE;
                if (c < 0x20 && c != '\t' && c != '\r' && c != '\n') return ISFALSE;
                if (c == 0x7f) return ISFALSE;
        }

        return ISTRUE;
}

static BOOK_OUTPUT_STAGE * book_output_stage(BOOK_SESSION * session) {
        if (!session) return NULL;

        if (!session->output_stage) {
                session->output_stage = calloc(1, sizeof(*session->output_stage));
        }

        return session->output_stage;
}

static int book_output_reserve(BOOK_OUTPUT_STAGE * stage) {
        BOOK_OUTPUT_RECORD * expanded;
        c_size_t next_capacity;

        if (!stage) return ABNORMAL;
        if (stage->count < stage->capacity) return NORMAL;
        if (stage->count >= BOOK_OUTPUT_RECORD_LIMIT) return BOOK_OUTPUT_LIMIT;

        next_capacity = stage->capacity ? stage->capacity * 2 : 8;
        if (next_capacity > BOOK_OUTPUT_RECORD_LIMIT) {
                next_capacity = BOOK_OUTPUT_RECORD_LIMIT;
        }

        expanded = realloc(
                stage->records,
                next_capacity * sizeof(*stage->records)
        );
        if (!expanded) return ABNORMAL;

        if (next_capacity > stage->capacity) {
                memset(
                        expanded + stage->capacity,
                        0x00,
                        (next_capacity - stage->capacity) * sizeof(*expanded)
                );
        }

        stage->records = expanded;
        stage->capacity = next_capacity;
        return NORMAL;
}

// @@ Stage one semantic result record for atomic durable commit.
int book_output_emit(
        BOOK_SESSION * session,
        const char * key,
        c_size_t key_length,
        const char * value,
        c_size_t value_length,
        book_output_color_t color
) {
        BOOK_OUTPUT_STAGE * stage;
        BOOK_OUTPUT_RECORD * record;
        int status;

        if (!session) return ABNORMAL;
        if (book_output_key_valid(key, key_length) != ISTRUE) return ABNORMAL;
        if (book_output_value_valid(value, value_length) != ISTRUE) return ABNORMAL;
        if (color < BOOK_OUTPUT_COLOR_DEFAULT || color > BOOK_OUTPUT_COLOR_GREY) return ABNORMAL;

        stage = book_output_stage(session);
        if (!stage) return ABNORMAL;

        if (
                stage->count >= BOOK_OUTPUT_RECORD_LIMIT
                || stage->staged_bytes + key_length + value_length > BOOK_OUTPUT_STAGED_LIMIT
        ) {
                return BOOK_OUTPUT_LIMIT;
        }

        status = book_output_reserve(stage);
        if (status != NORMAL) return status;

        record = &stage->records[stage->count];
        memset(record, 0x00, sizeof(*record));
        memcpy(record->key, key, key_length);
        memcpy(record->value, value, value_length);
        record->value_length = value_length;
        record->color = color;

        stage->count++;
        stage->staged_bytes += key_length + value_length;
        return NORMAL;
}

c_size_t book_output_count(const BOOK_SESSION * session) {
        if (!session || !session->output_stage) return 0;
        return session->output_stage->count;
}

void book_output_discard(BOOK_SESSION * session) {
        BOOK_OUTPUT_STAGE * stage;

        if (!session || !session->output_stage) return;
        stage = session->output_stage;

        if (stage->records) {
                memset(
                        stage->records,
                        0x00,
                        stage->capacity * sizeof(*stage->records)
                );
                free(stage->records);
                stage->records = NULL;
        }

        memset(stage, 0x00, sizeof(*stage));
        free(stage);
        session->output_stage = NULL;
        return;
}
