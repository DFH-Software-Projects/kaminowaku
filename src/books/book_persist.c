// Copyright 2026 Jamison A. Drapeau
#include "book_persist.h"
#include "book_output.h"
#include "kui.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define BOOK_OUT_MAGIC "KAMINOWAKU_BOOK_OUT\t1"
#define BOOK_OUT_LINE_LIMIT (BOOK_OUTPUT_VALUE_BLOCK * 2U + 256U)
#define BOOK_OUT_FILE_LIMIT (BOOK_OUTPUT_STAGED_LIMIT * 3U + 65536U)
#define BOOK_OUT_TEMP_ATTEMPTS 100U

#ifndef O_DIRECTORY
#define O_DIRECTORY 0
#endif

#ifndef O_NOFOLLOW
#define O_NOFOLLOW 0
#endif

typedef struct BOOK_RENDER_DATA {
        unsigned char       book_name[BOOK_NAME_BLOCK];
        BOOK_OUTPUT_RECORD *records;
        c_size_t            count;
        c_size_t            capacity;
} BOOK_RENDER_DATA;

static int book_write_all(int fd, const unsigned char * data, c_size_t length) {
        c_size_t offset = 0;

        if (fd < 0 || (!data && length > 0)) return ABNORMAL;

        while (offset < length) {
                ssize_t written = write(fd, data + offset, length - offset);

                if (written < 0) {
                        if (errno == EINTR) continue;
                        return ABNORMAL;
                }
                if (written == 0) return ABNORMAL;
                offset += (c_size_t)written;
        }

        return NORMAL;
}

static int book_write_text(int fd, const char * text) {
        if (!text) return ABNORMAL;
        return book_write_all(
                fd,
                (const unsigned char*)text,
                (c_size_t)strlen(text)
        );
}

static int book_hex_write(
        int fd,
        const unsigned char * data,
        c_size_t length
) {
        static const char HEX[] = "0123456789abcdef";
        unsigned char buffer[1024];
        c_size_t offset = 0;

        if (!data && length > 0) return ABNORMAL;

        while (offset < length) {
                c_size_t chunk = length - offset;

                if (chunk > sizeof(buffer) / 2U) chunk = sizeof(buffer) / 2U;

                for (c_size_t i = 0; i < chunk; i++) {
                        unsigned char value = data[offset + i];
                        buffer[i * 2U] = (unsigned char)HEX[(value >> 4) & 0x0fU];
                        buffer[i * 2U + 1U] = (unsigned char)HEX[value & 0x0fU];
                }

                if (book_write_all(fd, buffer, chunk * 2U) != NORMAL) {
                        return ABNORMAL;
                }
                offset += chunk;
        }

        return NORMAL;
}

static int book_key_safe(const unsigned char * key) {
        c_size_t length;

        if (!key) return ISFALSE;
        length = (c_size_t)strlen((const char*)key);
        if (length == 0 || length >= BOOK_OUTPUT_KEY_BLOCK) return ISFALSE;

        for (c_size_t i = 0; i < length; i++) {
                unsigned char c = key[i];

                if (c < 0x21 || c == 0x7f || c == '\t' || c == '\\') {
                        return ISFALSE;
                }
        }

        return ISTRUE;
}

static int book_target_directory_safe(const char * target_directory) {
        struct stat info;

        if (!target_directory || target_directory[0] == 0x00) return ISFALSE;
        if (lstat(target_directory, &info) != NORMAL) return ISFALSE;
        if (S_ISLNK(info.st_mode) || !S_ISDIR(info.st_mode)) return ISFALSE;
        return ISTRUE;
}

// @@ Compute final serialized size before any replacement temp file is opened
static int book_output_serialized_size(
        const BOOK_SESSION * session,
        c_size_t * size_out
) {
        const BOOK_OUTPUT_STAGE * stage;
        c_size_t size;

        if (!session || !size_out) return ABNORMAL;

        size = (c_size_t)strlen(BOOK_OUT_MAGIC) + 1U;
        size += 5U + (c_size_t)strlen((const char*)session->book_name) + 1U;
        stage = session->output_stage;

        if (stage) {
                for (c_size_t i = 0; i < stage->count; i++) {
                        const BOOK_OUTPUT_RECORD * record = &stage->records[i];
                        c_size_t key_length;
                        c_size_t color_length;
                        c_size_t record_size;

                        if (book_key_safe(record->key) != ISTRUE) return ABNORMAL;
                        key_length = (c_size_t)strlen((const char*)record->key);
                        color_length = (c_size_t)strlen(
                                book_output_color_name(record->color)
                        );

                        if (record->value_length > BOOK_OUT_FILE_LIMIT / 2U) {
                                return ABNORMAL;
                        }

                        record_size = 6U + color_length + 1U + key_length + 1U;
                        if (
                                record->value_length
                                > (SIZE_MAX - record_size - 1U) / 2U
                        ) return ABNORMAL;

                        record_size += record->value_length * 2U + 1U;
                        if (
                                size > BOOK_OUT_FILE_LIMIT
                                || record_size > BOOK_OUT_FILE_LIMIT - size
                        ) return ABNORMAL;

                        size += record_size;
                }
        }

        if (size > BOOK_OUT_FILE_LIMIT) return ABNORMAL;
        *size_out = size;
        return NORMAL;
}

static int book_open_temp(
        const unsigned char * final_path,
        uint64_t session_id,
        unsigned char * temp_path,
        c_size_t temp_path_size
) {
        for (unsigned int attempt = 0; attempt < BOOK_OUT_TEMP_ATTEMPTS; attempt++) {
                int written;
                int fd;

                memset(temp_path, 0x00, temp_path_size);
                written = snprintf(
                        (char*)temp_path,
                        temp_path_size,
                        "%s.tmp.%ld.%llu.%u",
                        final_path,
                        (long)getpid(),
                        (unsigned long long)session_id,
                        attempt
                );
                if (written <= 0 || (c_size_t)written >= temp_path_size) {
                        return -1;
                }

                fd = open(
                        (const char*)temp_path,
                        O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW,
                        0600
                );
                if (fd >= 0) return fd;
                if (errno != EEXIST) return -1;
        }

        return -1;
}

int book_output_commit(
        BOOK_SESSION * session,
        const char * target_directory
) {
        BOOK_OUTPUT_STAGE * stage;
        unsigned char final_path[MAX_PATH];
        unsigned char temp_path[MAX_PATH];
        c_size_t serialized_size = 0;
        int fd = -1;
        int dir_fd = -1;
        int written;

        if (!session || !target_directory) return ABNORMAL;
        if (session->termination != BOOK_TERM_COMPLETE) return ABNORMAL;
        if (book_target_directory_safe(target_directory) != ISTRUE) return ABNORMAL;
        if (book_output_serialized_size(session, &serialized_size) != NORMAL) {
                return ABNORMAL;
        }
        if (serialized_size > BOOK_OUT_FILE_LIMIT) return ABNORMAL;

        stage = session->output_stage;
        memset(final_path, 0x00, sizeof(final_path));
        memset(temp_path, 0x00, sizeof(temp_path));

        written = snprintf(
                (char*)final_path,
                sizeof(final_path),
                "%s%s%s.out",
                target_directory,
                target_directory[strlen(target_directory) - 1] == '/' ? "" : "/",
                session->book_name
        );
        if (written <= 0 || (c_size_t)written >= sizeof(final_path)) {
                return ABNORMAL;
        }

        fd = book_open_temp(
                final_path,
                session->session_id,
                temp_path,
                sizeof(temp_path)
        );
        if (fd < 0) return ABNORMAL;

        if (
                book_write_text(fd, BOOK_OUT_MAGIC "\n") != NORMAL
                || book_write_text(fd, "BOOK\t") != NORMAL
                || book_write_text(fd, (const char*)session->book_name) != NORMAL
                || book_write_text(fd, "\n") != NORMAL
        ) goto fail;

        if (stage) {
                for (c_size_t i = 0; i < stage->count; i++) {
                        BOOK_OUTPUT_RECORD * record = &stage->records[i];

                        if (book_key_safe(record->key) != ISTRUE) goto fail;
                        if (
                                book_write_text(fd, "FIELD\t") != NORMAL
                                || book_write_text(
                                        fd,
                                        book_output_color_name(record->color)
                                ) != NORMAL
                                || book_write_text(fd, "\t") != NORMAL
                                || book_write_text(
                                        fd,
                                        (const char*)record->key
                                ) != NORMAL
                                || book_write_text(fd, "\t") != NORMAL
                                || book_hex_write(
                                        fd,
                                        record->value,
                                        record->value_length
                                ) != NORMAL
                                || book_write_text(fd, "\n") != NORMAL
                        ) goto fail;
                }
        }

        if (fsync(fd) != NORMAL) goto fail;
        if (close(fd) != NORMAL) {
                fd = -1;
                unlink((const char*)temp_path);
                return ABNORMAL;
        }
        fd = -1;

        if (rename((const char*)temp_path, (const char*)final_path) != NORMAL) {
                unlink((const char*)temp_path);
                return ABNORMAL;
        }

        // Best-effort directory sync. The atomic replacement has already
        // happened at this point; a directory-sync failure cannot be rolled
        // back without violating the previous-output preservation guarantee.
        dir_fd = open(target_directory, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
        if (dir_fd >= 0) {
                (void)fsync(dir_fd);
                close(dir_fd);
        }

        return NORMAL;

fail:
        if (fd >= 0) close(fd);
        unlink((const char*)temp_path);
        return ABNORMAL;
}

static int book_hex_value(unsigned char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return 10 + c - 'a';
        if (c >= 'A' && c <= 'F') return 10 + c - 'A';
        return -1;
}

static int book_hex_decode(
        const unsigned char * text,
        unsigned char * output,
        c_size_t output_size,
        c_size_t * output_length
) {
        c_size_t length;

        if (!text || !output || !output_length || output_size == 0) {
                return ABNORMAL;
        }

        length = (c_size_t)strlen((const char*)text);
        if ((length % 2U) != 0 || length / 2U >= output_size) return ABNORMAL;

        for (c_size_t i = 0; i < length; i += 2U) {
                int high = book_hex_value(text[i]);
                int low = book_hex_value(text[i + 1U]);

                if (high < 0 || low < 0) return ABNORMAL;
                output[i / 2U] = (unsigned char)((high << 4) | low);
        }

        *output_length = length / 2U;
        output[*output_length] = 0x00;
        return NORMAL;
}

static const char * book_color_ansi(book_output_color_t color) {
        switch (color) {
                case BOOK_OUTPUT_COLOR_GREEN:  return ANSI_COLOR_GREEN;
                case BOOK_OUTPUT_COLOR_RED:    return ANSI_COLOR_RED;
                case BOOK_OUTPUT_COLOR_YELLOW: return ANSI_COLOR_YELLOW;
                case BOOK_OUTPUT_COLOR_GREY:   return RGB_COLOR_SOFT_GREY;
                case BOOK_OUTPUT_COLOR_DEFAULT:
                default:                       return "";
        }
}

static void book_heading(const unsigned char * name) {
        unsigned char heading[BOOK_NAME_BLOCK];
        c_size_t length;

        if (!name) return;
        memset(heading, 0x00, sizeof(heading));
        length = (c_size_t)strlen((const char*)name);
        if (length >= sizeof(heading)) length = sizeof(heading) - 1U;

        for (c_size_t i = 0; i < length; i++) {
                heading[i] = (unsigned char)toupper(name[i]);
        }

        kui_add_line("");
        kui_add_line(
                ANSI_COLOR_CYAN
                " %s:"
                ANSI_COLOR_RESET,
                heading
        );
        kui_add_line(
                RGB_COLOR_SOFT_GREY
                " ─────────────────────────────────────────────────────────"
                ANSI_COLOR_RESET
        );
}

static void book_render_data_free(BOOK_RENDER_DATA * data) {
        if (!data) return;

        if (data->records) {
                memset(
                        data->records,
                        0x00,
                        data->capacity * sizeof(*data->records)
                );
                free(data->records);
        }

        memset(data, 0x00, sizeof(*data));
}

static int book_render_reserve(BOOK_RENDER_DATA * data) {
        BOOK_OUTPUT_RECORD * expanded;
        c_size_t next;

        if (!data) return ABNORMAL;
        if (data->count < data->capacity) return NORMAL;
        if (data->count >= BOOK_OUTPUT_RECORD_LIMIT) return ABNORMAL;

        next = data->capacity ? data->capacity * 2U : 8U;
        if (next > BOOK_OUTPUT_RECORD_LIMIT) next = BOOK_OUTPUT_RECORD_LIMIT;

        expanded = realloc(data->records, next * sizeof(*expanded));
        if (!expanded) return ABNORMAL;

        memset(
                expanded + data->capacity,
                0x00,
                (next - data->capacity) * sizeof(*expanded)
        );
        data->records = expanded;
        data->capacity = next;
        return NORMAL;
}

static int book_filename_matches(
        const char * path,
        const unsigned char * book_name
) {
        const char * base;
        char expected[BOOK_NAME_BLOCK + 5U];
        int written;

        if (!path || !book_name) return ISFALSE;
        base = strrchr(path, '/');
        base = base ? base + 1 : path;

        memset(expected, 0x00, sizeof(expected));
        written = snprintf(
                expected,
                sizeof(expected),
                "%s.out",
                book_name
        );
        if (written <= 0 || (c_size_t)written >= sizeof(expected)) return ISFALSE;
        return strcmp(base, expected) == MATCH ? ISTRUE : ISFALSE;
}

static int book_output_has_magic(const char * path) {
        FILE * file;
        struct stat info;
        char line[sizeof(BOOK_OUT_MAGIC) + 2U];
        char * newline;
        int matches = ISFALSE;

        if (!path) return ISFALSE;

        if (
                lstat(path, &info) != NORMAL
                || !S_ISREG(info.st_mode)
                || S_ISLNK(info.st_mode)
        ) {
                return ISFALSE;
        }

        file = fopen(path, "r");
        if (!file) return ISFALSE;

        memset(line, 0x00, sizeof(line));
        if (fgets(line, sizeof(line), file)) {
                newline = strchr(line, '\n');
                if (newline) *newline = 0x00;
                if (strcmp(line, BOOK_OUT_MAGIC) == MATCH) {
                        matches = ISTRUE;
                }
        }

        fclose(file);
        return matches;
}

// @@ Parse the complete artifact before emitting any TUI content
static int book_parse_file(
        const char * path,
        BOOK_RENDER_DATA * parsed
) {
        FILE * file;
        struct stat info;
        char line[BOOK_OUT_LINE_LIMIT];
        int line_number = 0;

        if (!path || !parsed) return ABNORMAL;
        memset(parsed, 0x00, sizeof(*parsed));

        if (
                lstat(path, &info) != NORMAL
                || !S_ISREG(info.st_mode)
                || S_ISLNK(info.st_mode)
                || info.st_size < 0
                || (uint64_t)info.st_size > BOOK_OUT_FILE_LIMIT
        ) return ABNORMAL;

        file = fopen(path, "r");
        if (!file) return ABNORMAL;

        while (fgets(line, sizeof(line), file)) {
                char * newline;

                line_number++;
                newline = strchr(line, '\n');
                if (newline) *newline = 0x00;
                else if (!feof(file)) goto malformed;

                if (line_number == 1) {
                        if (strcmp(line, BOOK_OUT_MAGIC) != MATCH) goto malformed;
                        continue;
                }

                if (line_number == 2) {
                        if (strncmp(line, "BOOK\t", 5) != MATCH) goto malformed;
                        snprintf(
                                (char*)parsed->book_name,
                                sizeof(parsed->book_name),
                                "%s",
                                line + 5
                        );
                        if (
                                book_name_validate(parsed->book_name) != NORMAL
                                || book_filename_matches(
                                        path,
                                        parsed->book_name
                                ) != ISTRUE
                        ) goto malformed;
                        continue;
                }

                if (strncmp(line, "FIELD\t", 6) == MATCH) {
                        char * color_text = line + 6;
                        char * key = strchr(color_text, '\t');
                        char * value_hex;
                        book_output_color_t color;
                        BOOK_OUTPUT_RECORD * record;

                        if (!key) goto malformed;
                        *key++ = 0x00;
                        value_hex = strchr(key, '\t');
                        if (!value_hex) goto malformed;
                        *value_hex++ = 0x00;

                        if (
                                book_output_parse_color(color_text, &color) != NORMAL
                                || book_key_safe((const unsigned char*)key) != ISTRUE
                                || book_render_reserve(parsed) != NORMAL
                        ) goto malformed;

                        record = &parsed->records[parsed->count];
                        memset(record, 0x00, sizeof(*record));
                        snprintf(
                                (char*)record->key,
                                sizeof(record->key),
                                "%s",
                                key
                        );
                        if (
                                book_hex_decode(
                                        (const unsigned char*)value_hex,
                                        record->value,
                                        sizeof(record->value),
                                        &record->value_length
                                ) != NORMAL
                        ) goto malformed;
                        record->color = color;
                        parsed->count++;
                        continue;
                }

                goto malformed;
        }

        if (ferror(file)) goto malformed;
        fclose(file);

        if (
                line_number < 2
                || parsed->book_name[0] == 0x00
        ) {
                book_render_data_free(parsed);
                return ABNORMAL;
        }

        return NORMAL;

malformed:
        fclose(file);
        book_render_data_free(parsed);
        return ABNORMAL;
}

static void book_render_value(
        const BOOK_OUTPUT_RECORD * record,
        int8_t nested
) {
        const char * ansi;

        if (!record) return;
        ansi = book_color_ansi(record->color);

        if (nested == ISTRUE) {
                if (ansi[0] == 0x00) {
                        kui_add_line(
                                "     %.*s",
                                (int)record->value_length,
                                record->value
                        );
                } else {
                        kui_add_line(
                                "     %s%.*s" ANSI_COLOR_RESET,
                                ansi,
                                (int)record->value_length,
                                record->value
                        );
                }
                return;
        }

        if (ansi[0] == 0x00) {
                kui_add_line(
                        " %s: %.*s",
                        record->key,
                        (int)record->value_length,
                        record->value
                );
        } else {
                kui_add_line(
                        " %s: %s%.*s" ANSI_COLOR_RESET,
                        record->key,
                        ansi,
                        (int)record->value_length,
                        record->value
                );
        }
}

static int book_render_file(const char * path) {
        BOOK_RENDER_DATA parsed;
        unsigned char rendered[BOOK_OUTPUT_RECORD_LIMIT];

        memset(&parsed, 0x00, sizeof(parsed));
        memset(rendered, 0x00, sizeof(rendered));
        if (book_parse_file(path, &parsed) != NORMAL) return ABNORMAL;

        book_heading(parsed.book_name);

        for (c_size_t i = 0; i < parsed.count; i++) {
                c_size_t matches = 0;

                if (rendered[i] == ISTRUE) continue;

                for (c_size_t j = i; j < parsed.count; j++) {
                        if (
                                rendered[j] != ISTRUE
                                && strcmp(
                                        (const char*)parsed.records[i].key,
                                        (const char*)parsed.records[j].key
                                ) == MATCH
                        ) {
                                matches++;
                        }
                }

                if (matches <= 1) {
                        rendered[i] = ISTRUE;
                        book_render_value(
                                &parsed.records[i],
                                ISFALSE
                        );
                        continue;
                }

                kui_add_line(
                        " %s:",
                        parsed.records[i].key
                );

                for (c_size_t j = i; j < parsed.count; j++) {
                        if (
                                rendered[j] == ISTRUE
                                || strcmp(
                                        (const char*)parsed.records[i].key,
                                        (const char*)parsed.records[j].key
                                ) != MATCH
                        ) {
                                continue;
                        }

                        rendered[j] = ISTRUE;
                        book_render_value(
                                &parsed.records[j],
                                ISTRUE
                        );
                }
        }

        book_render_data_free(&parsed);
        return NORMAL;
}

static int book_name_compare(const void * left, const void * right) {
        const char * const * a = (const char * const *)left;
        const char * const * b = (const char * const *)right;
        return strcmp(*a, *b);
}

int book_output_directory_has_output(
        const char * target_directory
) {
        DIR * directory;
        struct dirent * entry;

        if (book_target_directory_safe(target_directory) != ISTRUE) {
                return ISFALSE;
        }

        directory = opendir(target_directory);
        if (!directory) return ISFALSE;

        while ((entry = readdir(directory)) != NULL) {
                c_size_t length = (c_size_t)strlen(entry->d_name);
                char path[MAX_PATH];
                int written;

                if (
                        length <= 4
                        || strcmp(
                                entry->d_name + length - 4,
                                ".out"
                        ) != MATCH
                ) {
                        continue;
                }

                memset(path, 0x00, sizeof(path));
                written = snprintf(
                        path,
                        sizeof(path),
                        "%s%s%s",
                        target_directory,
                        target_directory[strlen(target_directory) - 1] == '/' ? "" : "/",
                        entry->d_name
                );

                if (
                        written > 0
                        && (c_size_t)written < sizeof(path)
                        && book_output_has_magic(path) == ISTRUE
                ) {
                        closedir(directory);
                        return ISTRUE;
                }
        }

        closedir(directory);
        return ISFALSE;
}

int book_output_render_directory(const char * target_directory) {
        DIR * directory;
        struct dirent * entry;
        char ** names = NULL;
        c_size_t count = 0;
        c_size_t capacity = 0;
        int rendered = 0;

        if (book_target_directory_safe(target_directory) != ISTRUE) {
                return ABNORMAL;
        }

        directory = opendir(target_directory);
        if (!directory) return ABNORMAL;

        while ((entry = readdir(directory)) != NULL) {
                c_size_t length = (c_size_t)strlen(entry->d_name);
                char * copy;

                if (
                        length <= 4
                        || strcmp(entry->d_name + length - 4, ".out") != MATCH
                ) continue;

                {
                        char path[MAX_PATH];
                        int written;

                        memset(path, 0x00, sizeof(path));
                        written = snprintf(
                                path,
                                sizeof(path),
                                "%s%s%s",
                                target_directory,
                                target_directory[strlen(target_directory) - 1] == '/' ? "" : "/",
                                entry->d_name
                        );

                        if (
                                written <= 0
                                || (c_size_t)written >= sizeof(path)
                                || book_output_has_magic(path) != ISTRUE
                        ) {
                                continue;
                        }
                }

                if (count == capacity) {
                        c_size_t next = capacity ? capacity * 2U : 8U;
                        char ** expanded = realloc(names, next * sizeof(*expanded));

                        if (!expanded) break;
                        names = expanded;
                        capacity = next;
                }

                copy = strdup(entry->d_name);
                if (!copy) break;
                names[count++] = copy;
        }
        closedir(directory);

        qsort(names, count, sizeof(*names), book_name_compare);

        for (c_size_t i = 0; i < count; i++) {
                char path[MAX_PATH];
                int written;

                memset(path, 0x00, sizeof(path));
                written = snprintf(
                        path,
                        sizeof(path),
                        "%s%s%s",
                        target_directory,
                        target_directory[strlen(target_directory) - 1] == '/' ? "" : "/",
                        names[i]
                );

                if (
                        written > 0
                        && (c_size_t)written < sizeof(path)
                ) {
                        if (book_render_file(path) == NORMAL) rendered++;
                        else {
                                kui_add_line(
                                        NOTICE_WARNING "Skipping malformed or unsafe book output: %s",
                                        names[i]
                                );
                        }
                }

                free(names[i]);
        }
        free(names);
        return rendered;
}
