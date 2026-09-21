// Copyright 2026 Jamison A. Drapeau
#include "book_module.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef enum {
        BOOK_MODULE_EMPTY = 0,
        BOOK_MODULE_LOADING,
        BOOK_MODULE_LOADED
} book_module_status_t;

typedef enum {
        BOOK_MODULE_NAMESPACE_KAMI = 0,
        BOOK_MODULE_NAMESPACE_MODULES,
        BOOK_MODULE_NAMESPACE_USER
} book_module_namespace_t;

typedef struct BOOK_MODULE_ENTRY {
        unsigned char name[BOOK_MODULE_NAME_BLOCK];
        c_size_t name_length;

        BOOK_PROGRAM * program;
        BOOK_VALUE value;
        book_module_status_t status;
} BOOK_MODULE_ENTRY;

typedef struct BOOK_MODULE_STATE {
        BOOK_MODULE_ENTRY entries[BOOK_MODULE_LIMIT];
        c_size_t count;
        c_size_t depth;
        c_size_t source_bytes;
} BOOK_MODULE_STATE;

static int book_module_name_char(unsigned char value) {
        return (
                (value >= 'A' && value <= 'Z')
                || (value >= 'a' && value <= 'z')
                || (value >= '0' && value <= '9')
                || value == '_'
                || value == '-'
        ) ? ISTRUE : ISFALSE;
}

static int book_module_namespace(
        const unsigned char * name,
        c_size_t length,
        const unsigned char ** remainder,
        c_size_t * remainder_length,
        book_module_namespace_t * namespace
) {
        c_size_t offset;

        if (
                !name
                || !remainder
                || !remainder_length
                || !namespace
                || length == 0
                || length >= BOOK_MODULE_NAME_BLOCK
        ) {
                return ABNORMAL;
        }

        if (
                length > 5
                && memcmp(name, "kami.", 5) == MATCH
        ) {
                offset = 5;
                *namespace = BOOK_MODULE_NAMESPACE_KAMI;
        } else if (
                length > 8
                && memcmp(name, "modules.", 8) == MATCH
        ) {
                offset = 8;
                *namespace = BOOK_MODULE_NAMESPACE_MODULES;
        } else if (
                length > 5
                && memcmp(name, "user.", 5) == MATCH
        ) {
                offset = 5;
                *namespace = BOOK_MODULE_NAMESPACE_USER;
        } else {
                return ABNORMAL;
        }

        if (offset >= length) return ABNORMAL;

        {
                c_size_t segment_length = 0;

                for (c_size_t i = offset; i < length; i++) {
                        if (name[i] == '.') {
                                if (segment_length == 0) return ABNORMAL;
                                segment_length = 0;
                                continue;
                        }

                        if (book_module_name_char(name[i]) != ISTRUE) {
                                return ABNORMAL;
                        }

                        segment_length++;
                        if (segment_length >= BOOK_MODULE_NAME_BLOCK) {
                                return ABNORMAL;
                        }
                }

                if (segment_length == 0) return ABNORMAL;
        }

        *remainder = name + offset;
        *remainder_length = length - offset;
        return NORMAL;
}

static BOOK_MODULE_ENTRY * book_module_find(
        BOOK_MODULE_STATE * state,
        const unsigned char * name,
        c_size_t length
) {
        if (!state || !name) return NULL;

        for (c_size_t i = 0; i < state->count; i++) {
                if (
                        state->entries[i].name_length == length
                        && memcmp(
                                state->entries[i].name,
                                name,
                                length
                        ) == MATCH
                ) {
                        return &state->entries[i];
                }
        }

        return NULL;
}

static int book_module_root(
        BOOK_RUNTIME * runtime,
        book_module_namespace_t namespace,
        unsigned char * root,
        c_size_t root_size
) {
        BOOK_SESSION * session;
        int written;

        if (!runtime || !root || root_size == 0) return ABNORMAL;
        session = book_runtime_session(runtime);

        switch (namespace) {
                case BOOK_MODULE_NAMESPACE_KAMI:
                        written = snprintf(
                                (char*)root,
                                root_size,
                                "%s",
                                KAMI_SYSTEM_BOOKS_MAIN_DIR
                        );
                        break;

                case BOOK_MODULE_NAMESPACE_MODULES:
                        written = snprintf(
                                (char*)root,
                                root_size,
                                "%s",
                                KAMI_SYSTEM_BOOKS_MODULES_DIR
                        );
                        break;

                case BOOK_MODULE_NAMESPACE_USER:
                        if (
                                !session
                                || !session->prog_data
                                || session->prog_data->wd[0] == 0x00
                        ) {
                                return book_runtime_native_fail(
                                        runtime,
                                        "user module root is unavailable"
                                );
                        }

                        written = snprintf(
                                (char*)root,
                                root_size,
                                "%s%s",
                                session->prog_data->wd,
                                KAMI_USER_BOOK_MODULES_DIR
                        );
                        break;

                default:
                        return book_runtime_native_fail(
                                runtime,
                                "invalid Lua module namespace"
                        );
        }

        if (written <= 0 || (c_size_t)written >= root_size) {
                return book_runtime_native_fail(
                        runtime,
                        "module root path is too long"
                );
        }

        while (
                written > 1
                && root[written - 1] == '/'
        ) {
                root[--written] = 0x00;
        }

        return NORMAL;
}

// @@ Open module components without following symlinks. The namespace root
// is selected before traversal; each remaining dot becomes one directory
// boundary and the final component becomes <name>.lua.
static int book_module_open(
        BOOK_RUNTIME * runtime,
        book_module_namespace_t namespace,
        const unsigned char * remainder,
        c_size_t remainder_length,
        int * fd_out,
        c_size_t * source_length
) {
        unsigned char root[BOOK_MODULE_PATH_BLOCK];
        int directory_fd = -1;
        int next_fd = -1;
        c_size_t cursor = 0;

        if (
                !runtime
                || !remainder
                || remainder_length == 0
                || !fd_out
                || !source_length
        ) {
                return ABNORMAL;
        }

        memset(root, 0x00, sizeof(root));

        if (
                book_module_root(
                        runtime,
                        namespace,
                        root,
                        sizeof(root)
                ) != NORMAL
        ) {
                return ABNORMAL;
        }

        directory_fd = open(
                (const char*)root,
                O_RDONLY | O_DIRECTORY | O_NOFOLLOW
        );
        if (directory_fd < 0) {
                return book_runtime_native_fail(
                        runtime,
                        "Lua module namespace root is unavailable"
                );
        }

        while (cursor < remainder_length) {
                unsigned char component[BOOK_MODULE_NAME_BLOCK + 8U];
                c_size_t start = cursor;
                c_size_t length;
                int8_t final;

                while (
                        cursor < remainder_length
                        && remainder[cursor] != '.'
                ) {
                        cursor++;
                }

                length = cursor - start;
                final = cursor == remainder_length ? ISTRUE : ISFALSE;

                if (
                        length == 0
                        || length >= BOOK_MODULE_NAME_BLOCK
                ) {
                        close(directory_fd);
                        return book_runtime_native_fail(
                                runtime,
                                "invalid Lua module component"
                        );
                }

                memset(component, 0x00, sizeof(component));
                memcpy(component, remainder + start, length);

                if (final == ISTRUE) {
                        struct stat info;
                        ssize_t written;

                        memcpy(component + length, ".lua", 4);
                        component[length + 4U] = 0x00;

                        next_fd = openat(
                                directory_fd,
                                (const char*)component,
                                O_RDONLY | O_NOFOLLOW
                        );
                        close(directory_fd);
                        directory_fd = -1;

                        if (next_fd < 0) {
                                return book_runtime_native_fail(
                                        runtime,
                                        "Lua module was not found"
                                );
                        }

                        memset(&info, 0x00, sizeof(info));
                        if (
                                fstat(next_fd, &info) != NORMAL
                                || !S_ISREG(info.st_mode)
                                || info.st_size < 0
                                || (uint64_t)info.st_size
                                        > BOOK_LEXER_SOURCE_LIMIT
                        ) {
                                close(next_fd);
                                return book_runtime_native_fail(
                                        runtime,
                                        "Lua module is not a bounded regular file"
                                );
                        }

                        *source_length = (c_size_t)info.st_size;
                        *fd_out = next_fd;
                        return NORMAL;
                }

                next_fd = openat(
                        directory_fd,
                        (const char*)component,
                        O_RDONLY | O_DIRECTORY | O_NOFOLLOW
                );
                if (next_fd < 0) {
                        close(directory_fd);
                        return book_runtime_native_fail(
                                runtime,
                                "Lua module directory was not found"
                        );
                }

                close(directory_fd);
                directory_fd = next_fd;
                next_fd = -1;
                cursor++;
        }

        if (directory_fd >= 0) close(directory_fd);
        return book_runtime_native_fail(
                runtime,
                "invalid Lua module path"
        );
}

static int book_module_read(
        BOOK_RUNTIME * runtime,
        int fd,
        c_size_t source_length,
        unsigned char ** source
) {
        unsigned char * buffer;
        c_size_t offset = 0;

        if (!runtime || fd < 0 || !source) return ABNORMAL;

        buffer = calloc(source_length ? source_length : 1U, 1);
        if (!buffer) {
                return book_runtime_native_fail(
                        runtime,
                        "failed to allocate Lua module source"
                );
        }

        while (offset < source_length) {
                ssize_t received = read(
                        fd,
                        buffer + offset,
                        source_length - offset
                );

                if (received < 0) {
                        if (errno == EINTR) continue;
                        free(buffer);
                        return book_runtime_native_fail(
                                runtime,
                                "failed to read Lua module"
                        );
                }

                if (received == 0) {
                        free(buffer);
                        return book_runtime_native_fail(
                                runtime,
                                "short read while loading Lua module"
                        );
                }

                offset += (c_size_t)received;
        }

        *source = buffer;
        return NORMAL;
}

static int book_module_parse(
        BOOK_RUNTIME * runtime,
        const unsigned char * source,
        c_size_t source_length,
        BOOK_PROGRAM * program
) {
        BOOK_LEXER_RESULT lexer;

        if (!runtime || !program || (!source && source_length > 0)) {
                return ABNORMAL;
        }

        memset(&lexer, 0x00, sizeof(lexer));
        memset(program, 0x00, sizeof(*program));

        if (
                book_lexer_tokenize(
                        source,
                        source_length,
                        &lexer
                ) != NORMAL
        ) {
                char detail[BOOK_RUNTIME_ERROR_BLOCK];

                snprintf(
                        detail,
                        sizeof(detail),
                        "module lexer error @ %u:%u: %s",
                        lexer.error_line,
                        lexer.error_column,
                        lexer.error[0] ? lexer.error : "invalid Lua source"
                );
                book_lexer_free(&lexer);
                return book_runtime_native_fail(runtime, detail);
        }

        if (book_parser_parse(&lexer, program) != NORMAL) {
                char detail[BOOK_RUNTIME_ERROR_BLOCK];

                snprintf(
                        detail,
                        sizeof(detail),
                        "module parser error @ %u:%u: %s",
                        program->error_line,
                        program->error_column,
                        program->error[0] ? program->error : "invalid Lua source"
                );
                book_program_free(program);
                return book_runtime_native_fail(runtime, detail);
        }

        return NORMAL;
}

int book_module_require(
        BOOK_RUNTIME * runtime,
        const unsigned char * name,
        c_size_t name_length,
        BOOK_VALUE * value
) {
        BOOK_MODULE_STATE * state;
        BOOK_MODULE_ENTRY * entry;
        const unsigned char * remainder;
        c_size_t remainder_length;
        book_module_namespace_t namespace;
        int fd = -1;
        c_size_t source_length = 0;
        unsigned char * source = NULL;
        BOOK_PROGRAM * program = NULL;
        BOOK_MULTI_VALUE returns;

        if (!runtime || !name || !value) return ABNORMAL;

        state = (BOOK_MODULE_STATE*)book_runtime_module_state(runtime);
        if (!state) {
                return book_runtime_native_fail(
                        runtime,
                        "Lua module resolver is not initialized"
                );
        }

        if (
                book_module_namespace(
                        name,
                        name_length,
                        &remainder,
                        &remainder_length,
                        &namespace
                ) != NORMAL
        ) {
                return book_runtime_native_fail(
                        runtime,
                        "require() permits only canonical kami.*, modules.*, or user.* modules"
                );
        }

        entry = book_module_find(state, name, name_length);
        if (entry) {
                if (entry->status == BOOK_MODULE_LOADING) {
                        return book_runtime_native_fail(
                                runtime,
                                "circular Lua module import detected"
                        );
                }

                if (entry->status == BOOK_MODULE_LOADED) {
                        *value = entry->value;
                        return NORMAL;
                }
        }

        if (state->count >= BOOK_MODULE_LIMIT) {
                return book_runtime_native_fail(
                        runtime,
                        "Lua module count limit exceeded"
                );
        }

        if (state->depth >= BOOK_MODULE_DEPTH_LIMIT) {
                return book_runtime_native_fail(
                        runtime,
                        "Lua module import depth limit exceeded"
                );
        }

        if (!entry) {
                entry = &state->entries[state->count++];
                memset(entry, 0x00, sizeof(*entry));
                memcpy(entry->name, name, name_length);
                entry->name[name_length] = 0x00;
                entry->name_length = name_length;
        }

        entry->status = BOOK_MODULE_LOADING;
        state->depth++;

        if (
                book_module_open(
                        runtime,
                        namespace,
                        remainder,
                        remainder_length,
                        &fd,
                        &source_length
                ) != NORMAL
        ) {
                state->depth--;
                return ABNORMAL;
        }

        if (
                source_length > BOOK_MODULE_SOURCE_LIMIT
                        - state->source_bytes
        ) {
                close(fd);
                state->depth--;
                return book_runtime_native_fail(
                        runtime,
                        "Lua module source budget exceeded"
                );
        }

        if (
                book_module_read(
                        runtime,
                        fd,
                        source_length,
                        &source
                ) != NORMAL
        ) {
                close(fd);
                state->depth--;
                return ABNORMAL;
        }

        if (close(fd) != NORMAL) {
                memset(source, 0x00, source_length);
                free(source);
                state->depth--;
                return book_runtime_native_fail(
                        runtime,
                        "failed to close Lua module"
                );
        }
        fd = -1;

        program = calloc(1, sizeof(*program));
        if (!program) {
                memset(source, 0x00, source_length);
                free(source);
                state->depth--;
                return book_runtime_native_fail(
                        runtime,
                        "failed to allocate Lua module program"
                );
        }

        if (
                book_module_parse(
                        runtime,
                        source,
                        source_length,
                        program
                ) != NORMAL
        ) {
                memset(source, 0x00, source_length);
                free(source);
                free(program);
                state->depth--;
                return ABNORMAL;
        }

        memset(source, 0x00, source_length);
        free(source);
        source = NULL;

        state->source_bytes += source_length;
        entry->program = program;

        memset(&returns, 0x00, sizeof(returns));

        if (
                book_runtime_execute_module(
                        runtime,
                        program,
                        namespace == BOOK_MODULE_NAMESPACE_KAMI
                                ? ISTRUE
                                : ISFALSE,
                        &returns
                ) != NORMAL
        ) {
                state->depth--;
                return ABNORMAL;
        }

        entry->value = returns.count > 0
                && returns.values[0].type != BOOK_VALUE_NIL
                ? returns.values[0]
                : book_runtime_boolean(ISTRUE);

        entry->status = BOOK_MODULE_LOADED;
        state->depth--;
        *value = entry->value;
        return NORMAL;
}

static int book_module_native_require(
        BOOK_RUNTIME * runtime,
        const BOOK_VALUE * arguments,
        c_size_t argument_count,
        BOOK_MULTI_VALUE * returns
) {
        const unsigned char * name;
        c_size_t name_length;
        BOOK_VALUE value;

        if (
                !runtime
                || !returns
                || argument_count != 1
                || book_value_string(
                        &arguments[0],
                        &name,
                        &name_length
                ) != NORMAL
        ) {
                return book_runtime_native_fail(
                        runtime,
                        "require() expects exactly one module-name string"
                );
        }

        if (
                book_module_require(
                        runtime,
                        name,
                        name_length,
                        &value
                ) != NORMAL
        ) {
                return ABNORMAL;
        }

        memset(returns, 0x00, sizeof(*returns));
        returns->values[0] = value;
        returns->count = 1;
        return NORMAL;
}

int book_modules_install(BOOK_RUNTIME * runtime) {
        BOOK_MODULE_STATE * state;

        if (!runtime) return ABNORMAL;
        if (book_runtime_module_state(runtime)) return NORMAL;

        state = calloc(1, sizeof(*state));
        if (!state) {
                return book_runtime_native_fail(
                        runtime,
                        "failed to allocate Lua module resolver"
                );
        }

        book_runtime_set_module_state(runtime, state);

        if (
                book_runtime_register_global(
                        runtime,
                        "require",
                        book_module_native_require
                ) != NORMAL
        ) {
                book_modules_destroy(runtime);
                return ABNORMAL;
        }

        return NORMAL;
}

void book_modules_destroy(BOOK_RUNTIME * runtime) {
        BOOK_MODULE_STATE * state;

        if (!runtime) return;
        state = (BOOK_MODULE_STATE*)book_runtime_module_state(runtime);
        if (!state) return;

        for (c_size_t i = 0; i < state->count; i++) {
                if (state->entries[i].program) {
                        book_program_free(state->entries[i].program);
                        free(state->entries[i].program);
                        state->entries[i].program = NULL;
                }
        }

        memset(state, 0x00, sizeof(*state));
        free(state);
        book_runtime_set_module_state(runtime, NULL);
}
