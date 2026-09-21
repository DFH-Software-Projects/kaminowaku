// Copyright 2026 Jamison A. Drapeau
#include "tool_exec.h"
#include "tool_pty.h"
#include "kui.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static int tool_exec_name_valid(const char * tool_name) {
        if (!tool_name || tool_name[0] == 0x00) return ISFALSE;
        if (strcmp(tool_name, ".") == MATCH || strcmp(tool_name, "..") == MATCH) return ISFALSE;
        if (strchr(tool_name, '/')) return ISFALSE;

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
                return ISFALSE;
        }

        return ISTRUE;
}

static int tool_exec_registry_path(
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

static int tool_exec_resolve(
        const char * tool_name,
        char * executable_path,
        c_size_t executable_path_size
) {
        char registry_path[MAX_PATH];
        struct stat link_info;
        struct stat executable_info;

        if (
                !tool_name
                || !executable_path
                || executable_path_size == 0
        ) {
                return ABNORMAL;
        }

        if (tool_exec_name_valid(tool_name) != ISTRUE) {
                kui_add_line(
                        NOTICE_WARNING
                        "Invalid or reserved external tool name."
                );
                return ABNORMAL;
        }

        memset(registry_path, 0x00, sizeof(registry_path));
        if (
                tool_exec_registry_path(
                        tool_name,
                        registry_path,
                        sizeof(registry_path)
                ) != NORMAL
        ) {
                kui_add_line(
                        NOTICE_ERROR
                        "Tool registry path exceeds the supported path length."
                );
                return ABNORMAL;
        }

        if (lstat(registry_path, &link_info) != NORMAL) {
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
                                "Unable to inspect tool registration "
                                ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET ".",
                                tool_name
                        );
                }
                return ABNORMAL;
        }

        if (!S_ISLNK(link_info.st_mode)) {
                kui_add_line(
                        NOTICE_ERROR
                        "Refusing to execute " ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET
                        " because the toolbox entry is not a symlink.",
                        tool_name
                );
                return ABNORMAL;
        }

        memset(executable_path, 0x00, executable_path_size);
        if (!realpath(registry_path, executable_path)) {
                kui_add_line_and_render(
                        NOTICE_WARNING
                        "Tool " ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET
                        " has a stale or invalid registration.",
                        tool_name
                );
                return ABNORMAL;
        }

        if (stat(executable_path, &executable_info) != NORMAL) {
                kui_add_line(
                        NOTICE_ERROR
                        "Unable to inspect executable for "
                        ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET ".",
                        tool_name
                );
                return ABNORMAL;
        }

        if (!S_ISREG(executable_info.st_mode)) {
                kui_add_line(
                        NOTICE_ERROR
                        "Refusing to execute " ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET
                        " because its registration does not resolve to a regular file.",
                        tool_name
                );
                return ABNORMAL;
        }

        if (access(executable_path, X_OK) != NORMAL) {
                kui_add_line(
                        NOTICE_ERROR
                        "Registered tool " ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET
                        " is no longer executable.",
                        tool_name
                );
                return ABNORMAL;
        }

        return NORMAL;
}

int tool_exec_run(_carry_forward * _prog_data) {
        const char * tool_name;
        char executable_path[MAX_PATH];
        struct stat directory_info;
        char ** child_argv;
        int status = 0;

        if (
                !_prog_data
                || !_prog_data->cmd_tokens
                || _prog_data->cmd_tokens_count == 0
                || !_prog_data->cmd_tokens[0]
        ) {
                return ABNORMAL;
        }

        if (
                _prog_data->active_project_active_target_context != ISTRUE
                || !_prog_data->active_project_active_target
                || !_prog_data->active_project_active_target_directory
        ) {
                kui_add_line(
                        NOTICE_WARNING
                        "External tool execution requires an active target context."
                );
                return ABNORMAL;
        }

        if (
                stat(
                        _prog_data->active_project_active_target_directory,
                        &directory_info
                ) != NORMAL
                || !S_ISDIR(directory_info.st_mode)
        ) {
                kui_add_line(
                        NOTICE_ERROR
                        "Active target directory is unavailable."
                );
                return ABNORMAL;
        }

        tool_name = (const char*)_prog_data->cmd_tokens[0];
        memset(executable_path, 0x00, sizeof(executable_path));
        if (
                tool_exec_resolve(
                        tool_name,
                        executable_path,
                        sizeof(executable_path)
                ) != NORMAL
        ) {
                return ABNORMAL;
        }

        child_argv = calloc(
                _prog_data->cmd_tokens_count + 1,
                sizeof(*child_argv)
        );
        if (!child_argv) {
                kui_add_line(
                        NOTICE_ERROR
                        "Unable to allocate external tool arguments."
                );
                return ABNORMAL;
        }

        for (c_size_t i = 0; i < _prog_data->cmd_tokens_count; i++) {
                child_argv[i] = (char*)_prog_data->cmd_tokens[i];
        }
        child_argv[_prog_data->cmd_tokens_count] = NULL;

        if (
                tool_pty_run(
                        tool_name,
                        executable_path,
                        child_argv,
                        _prog_data->active_project_active_target_directory,
                        &status
                ) != NORMAL
        ) {
                free(child_argv);
                return ABNORMAL;
        }

        free(child_argv);

        if (WIFEXITED(status)) {
                int exit_status = WEXITSTATUS(status);

                if (exit_status == 0) {
                        kui_add_line_and_render(
                                NOTICE_SUCCESS
                                "Tool " ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET
                                " exited with status 0.",
                                tool_name
                        );
                        return NORMAL;
                }

                kui_add_line_and_render(
                        NOTICE_WARNING
                        "Tool " ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET
                        " exited with status %d.",
                        tool_name,
                        exit_status
                );
                return ABNORMAL;
        }

        if (WIFSIGNALED(status)) {
                kui_add_line_and_render(
                        NOTICE_WARNING
                        "Tool " ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET
                        " terminated by signal %d.",
                        tool_name,
                        WTERMSIG(status)
                );
                return ABNORMAL;
        }

        kui_add_line(
                NOTICE_WARNING
                "Tool " ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET
                " ended without a normal exit status.",
                tool_name
        );
        return ABNORMAL;
}
