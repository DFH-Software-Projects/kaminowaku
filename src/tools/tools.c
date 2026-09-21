// Copyright 2026 Jamison A. Drapeau
#include "tools.h"
#include "kui.h"
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int tools_name_reserved(const char * tool_name) {
        if (!tool_name) return ISTRUE;

        if (
                strcmp(tool_name, "back") == MATCH
                || strcmp(tool_name, "help") == MATCH
                || strcmp(tool_name, "list") == MATCH
                || strcmp(tool_name, "add") == MATCH
                || strcmp(tool_name, "del") == MATCH
                || strcmp(tool_name, "exit") == MATCH
                || strcmp(tool_name, "h") == MATCH
                || strcmp(tool_name, "n") == MATCH
                || strcmp(tool_name, "l") == MATCH
                || strcmp(tool_name, "u") == MATCH
                || strcmp(tool_name, "d") == MATCH
                || strcmp(tool_name, "dd") == MATCH
                || strcmp(tool_name, "a") == MATCH
                || strcmp(tool_name, "t") == MATCH
                || strcmp(tool_name, "p") == MATCH
                || strcmp(tool_name, "s") == MATCH
                || strcmp(tool_name, "b") == MATCH
                || strcmp(tool_name, "i") == MATCH
                || strcmp(tool_name, "r") == MATCH
        ) {
                return ISTRUE;
        }

        return ISFALSE;
}

static int tools_name_valid(const char * tool_name) {
        if (!tool_name || tool_name[0] == 0x00) return ISFALSE;
        if (strcmp(tool_name, ".") == MATCH || strcmp(tool_name, "..") == MATCH) return ISFALSE;
        if (strchr(tool_name, '/')) return ISFALSE;
        return ISTRUE;
}

static int tools_registry_path(
        const char * tool_name,
        char * output,
        c_size_t output_size
) {
        int written;

        if (!tool_name || !output || output_size == 0) return ABNORMAL;

        written = snprintf(
                output,
                output_size,
                "%s%s",
                KAMI_USER_TOOLS_DIR,
                tool_name
        );
        if (written < 0 || (c_size_t)written >= output_size) return ABNORMAL;

        return NORMAL;
}

static const char * tools_path_basename(const char * executable_path) {
        const char * slash;

        if (!executable_path) return NULL;

        slash = strrchr(executable_path, '/');
        if (!slash) return executable_path;
        return slash + 1;
}

static int tools_list_compare(const void * left, const void * right) {
        const char * const * a = (const char * const *)left;
        const char * const * b = (const char * const *)right;

        return strcmp(*a, *b);
}

void tools_print_usage(void) {
        kui_add_line(
                "< Usage: tool [ add " ANSI_COLOR_CYAN "<absolute-path>" ANSI_COLOR_RESET
                " | list | del " ANSI_COLOR_CYAN "<tool-name>" ANSI_COLOR_RESET " ]"
        );
        return;
}

static void tools_add_path(const char * executable_path) {
        const char * tool_name;
        char resolved_path[MAX_PATH];
        char registry_path[MAX_PATH];
        struct stat info;

        if (!executable_path) {
                tools_print_usage();
                return;
        }

        if (executable_path[0] != '/') {
                kui_add_line(
                        NOTICE_WARNING
                        "Tool paths must be absolute."
                );
                return;
        }

        memset(resolved_path, 0x00, sizeof(resolved_path));
        if (!realpath(executable_path, resolved_path)) {
                kui_add_line(
                        NOTICE_ERROR
                        "Could not resolve tool path " ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET ".",
                        executable_path
                );
                return;
        }

        if (stat(resolved_path, &info) != NORMAL) {
                kui_add_line(
                        NOTICE_ERROR
                        "Could not inspect tool path " ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET ".",
                        resolved_path
                );
                return;
        }

        if (!S_ISREG(info.st_mode)) {
                kui_add_line(
                        NOTICE_WARNING
                        "Tool path must resolve to a regular file."
                );
                return;
        }

        if (access(resolved_path, X_OK) != NORMAL) {
                kui_add_line(
                        NOTICE_WARNING
                        "Tool path is not executable."
                );
                return;
        }

        tool_name = tools_path_basename(executable_path);
        if (!tools_name_valid(tool_name)) {
                kui_add_line(
                        NOTICE_WARNING
                        "Tool path does not contain a valid executable name."
                );
                return;
        }

        if (tools_name_reserved(tool_name) == ISTRUE) {
                kui_add_line(
                        NOTICE_WARNING
                        "Tool name " ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET
                        " is reserved by the tool context.",
                        tool_name
                );
                return;
        }

        memset(registry_path, 0x00, sizeof(registry_path));
        if (
                tools_registry_path(
                        tool_name,
                        registry_path,
                        sizeof(registry_path)
                ) != NORMAL
        ) {
                kui_add_line(
                        NOTICE_ERROR
                        "Tool registry path exceeds the supported path length."
                );
                return;
        }

        if (lstat(registry_path, &info) == NORMAL) {
                kui_add_line(
                        NOTICE_WARNING
                        "Tool " ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET
                        " is already registered.",
                        tool_name
                );
                return;
        }

        if (errno != ENOENT) {
                kui_add_line(
                        NOTICE_ERROR
                        "Unable to inspect toolbox entry for "
                        ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET ".",
                        tool_name
                );
                return;
        }

        if (symlink(resolved_path, registry_path) != NORMAL) {
                kui_add_line(
                        NOTICE_ERROR
                        "Failed to register tool "
                        ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET ".",
                        tool_name
                );
                return;
        }

        kui_add_line(
                NOTICE_SUCCESS
                "Tool " ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET
                " registered -> " ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET ".",
                tool_name,
                resolved_path
        );
        return;
}

void tools_add(_carry_forward * _prog_data) {
        if (
                !_prog_data
                || _prog_data->cmd_tokens_count != 3
                || !_prog_data->cmd_tokens[2]
        ) {
                tools_print_usage();
                return;
        }

        tools_add_path((const char*)_prog_data->cmd_tokens[2]);
        return;
}

void tools_add_context(_carry_forward * _prog_data) {
        if (
                !_prog_data
                || _prog_data->cmd_tokens_count != 2
                || !_prog_data->cmd_tokens[1]
        ) {
                kui_add_line(
                        "< Usage: add " ANSI_COLOR_CYAN "<absolute-path>" ANSI_COLOR_RESET
                );
                return;
        }

        tools_add_path((const char*)_prog_data->cmd_tokens[1]);
        return;
}

void tools_list(void) {
        DIR * directory;
        struct dirent * entry;
        char ** entries = NULL;
        c_size_t count = 0;
        c_size_t capacity = 0;

        directory = opendir(KAMI_USER_TOOLS_DIR);
        if (!directory) {
                kui_add_line(
                        NOTICE_ERROR
                        "Unable to open the Kaminowaku tools directory."
                );
                return;
        }

        while ((entry = readdir(directory)) != NULL) {
                char ** resized;

                if (
                        strcmp(entry->d_name, ".") == MATCH
                        || strcmp(entry->d_name, "..") == MATCH
                ) {
                        continue;
                }

                if (count == capacity) {
                        c_size_t next_capacity = capacity == 0 ? 8 : capacity * 2;

                        resized = realloc(entries, next_capacity * sizeof(*entries));
                        if (!resized) {
                                for (c_size_t i = 0; i < count; i++) free(entries[i]);
                                free(entries);
                                closedir(directory);
                                kui_add_line(
                                        NOTICE_ERROR
                                        "Unable to allocate tool listing."
                                );
                                return;
                        }

                        entries = resized;
                        capacity = next_capacity;
                }

                entries[count] = strdup(entry->d_name);
                if (!entries[count]) {
                        for (c_size_t i = 0; i < count; i++) free(entries[i]);
                        free(entries);
                        closedir(directory);
                        kui_add_line(
                                NOTICE_ERROR
                                "Unable to allocate tool listing."
                        );
                        return;
                }

                count++;
        }

        closedir(directory);

        if (count == 0) {
                free(entries);
                kui_add_line(NOTICE_INFO "No external tools registered.");
                return;
        }

        qsort(entries, count, sizeof(*entries), tools_list_compare);

        for (c_size_t i = 0; i < count; i++) {
                char registry_path[MAX_PATH];
                char target_path[MAX_PATH];
                struct stat link_info;
                struct stat target_info;
                ssize_t target_length;

                memset(registry_path, 0x00, sizeof(registry_path));
                memset(target_path, 0x00, sizeof(target_path));

                if (
                        tools_registry_path(
                                entries[i],
                                registry_path,
                                sizeof(registry_path)
                        ) != NORMAL
                ) {
                        kui_add_line(
                                NOTICE_WARNING
                                ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET
                                " has an invalid registry path.",
                                entries[i]
                        );
                        free(entries[i]);
                        continue;
                }

                if (
                        lstat(registry_path, &link_info) != NORMAL
                        || !S_ISLNK(link_info.st_mode)
                ) {
                        kui_add_line(
                                NOTICE_WARNING
                                ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET
                                " is not a valid toolbox symlink.",
                                entries[i]
                        );
                        free(entries[i]);
                        continue;
                }

                target_length = readlink(
                        registry_path,
                        target_path,
                        sizeof(target_path) - 1
                );
                if (target_length < 0) {
                        kui_add_line(
                                NOTICE_WARNING
                                ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET
                                " could not be resolved.",
                                entries[i]
                        );
                        free(entries[i]);
                        continue;
                }

                target_path[target_length] = 0x00;

                if (stat(registry_path, &target_info) != NORMAL) {
                        kui_add_line(
                                ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET
                                " -> %s " ANSI_COLOR_RESET "["
                                RGB_COLOR_BRIGHT_YELLOW "stale" ANSI_COLOR_RESET "]",
                                entries[i],
                                target_path
                        );
                } else {
                        kui_add_line(
                                ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET
                                " -> %s",
                                entries[i],
                                target_path
                        );
                }

                free(entries[i]);
        }

        free(entries);
        return;
}

static void tools_del_name(const char * tool_name) {
        char registry_path[MAX_PATH];
        struct stat info;

        if (!tool_name) {
                tools_print_usage();
                return;
        }

        if (!tools_name_valid(tool_name)) {
                kui_add_line(
                        NOTICE_WARNING
                        "Tool deletion requires a registered tool name, not a path."
                );
                return;
        }

        memset(registry_path, 0x00, sizeof(registry_path));
        if (
                tools_registry_path(
                        tool_name,
                        registry_path,
                        sizeof(registry_path)
                ) != NORMAL
        ) {
                kui_add_line(
                        NOTICE_ERROR
                        "Tool registry path exceeds the supported path length."
                );
                return;
        }

        if (lstat(registry_path, &info) != NORMAL) {
                if (errno == ENOENT) {
                        kui_add_line(
                                NOTICE_WARNING
                                "Tool " ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET
                                " is not registered.",
                                tool_name
                        );
                } else {
                        kui_add_line(
                                NOTICE_ERROR
                                "Unable to inspect tool "
                                ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET ".",
                                tool_name
                        );
                }
                return;
        }

        if (!S_ISLNK(info.st_mode)) {
                kui_add_line(
                        NOTICE_ERROR
                        "Refusing to delete " ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET
                        " because the toolbox entry is not a symlink.",
                        tool_name
                );
                return;
        }

        if (unlink(registry_path) != NORMAL) {
                kui_add_line(
                        NOTICE_ERROR
                        "Failed to remove tool registration "
                        ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET ".",
                        tool_name
                );
                return;
        }

        kui_add_line(
                NOTICE_SUCCESS
                "Tool " ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET
                " removed from the toolbox.",
                tool_name
        );
        return;
}

void tools_del(_carry_forward * _prog_data) {
        if (
                !_prog_data
                || _prog_data->cmd_tokens_count != 3
                || !_prog_data->cmd_tokens[2]
        ) {
                tools_print_usage();
                return;
        }

        tools_del_name((const char*)_prog_data->cmd_tokens[2]);
        return;
}

void tools_del_context(_carry_forward * _prog_data) {
        if (
                !_prog_data
                || _prog_data->cmd_tokens_count != 2
                || !_prog_data->cmd_tokens[1]
        ) {
                kui_add_line(
                        "< Usage: del " ANSI_COLOR_CYAN "<tool-name>" ANSI_COLOR_RESET
                );
                return;
        }

        tools_del_name((const char*)_prog_data->cmd_tokens[1]);
        return;
}

