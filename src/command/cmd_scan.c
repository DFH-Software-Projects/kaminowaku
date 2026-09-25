// Copyright 2026 Jamison A. Drapeau
#include "kui.h"
#include "help.h"
#include "cmd_scan.h"
#include "helpers.h"
#include "projects.h"
#include "targets.h"
#include "interfaces.h"
#include "profile.h"
#include "ksping.h"
#include "kresolve.h"
#include "kportscan.h"
#include "books.h"
#include "tools.h"
#include "tool_exec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

/*
        - READ IN ENTIRETY
        - This is probably the most important code block in the entire program.
        - It takes in command data from the carry forward structure and processes it into arguments.

        I was going to use recurssive calls but they are not needed. Simply follow
        the flow of logic relative to the global data being handled by the entire
        program. In particular, _prog_data->f_type is used to handle extra prints
        done by first printing the banner in prompt and then resetting it in
        this file at the end of each code block.

        So the only reason I don't do something like returns is to keep things a little
        neater. In essence we want to print the banner out before every input and output.
        This flow with the global variable is a LITTLE like goto statements but it works.
        This way we keep the banner printing to a SINGLE function rather than replicating
        the call every time we output. We also have the option to recurse if needed.

        I was initially using an if/else ladder but the switch statement bypasses an entire
        logical check that would be needed in each if/esle, though it does not look quite
        as clean. Working like this allows us to create little "microfunctions" without all
        of the extra overhead as well.

        ADDITIONAL NOTES:

                - Process command token logic here. (e.g. Is this an integer? Is this a valid token?)
                - Process ALL other logic in command include files. (e.g. Does this directory exist? Is this a valid target index?)

        ---------------------------------------------------------------------------------------
        For help adding your own stuff follow the @@ marked comments with the associated steps.
        ---------------------------------------------------------------------------------------
*/

// DEBUG CODE: Display the f_type value
void f_type_debug_integer_display (_carry_forward * _prog_data) {
        kui_add_line(
                "!["
                ANSI_COLOR_MAGENTA
                "cmd_scan()"
                ANSI_COLOR_RESET
                "]> " ANSI_COLOR_CYAN "_prog_data->f_type" ANSI_COLOR_RESET " match at "
                ANSI_COLOR_MAGENTA
                "%d"
                ANSI_COLOR_RESET
                , _prog_data->f_type
        );
}

// COMMAND SCAN: Primary Function
void cmd_scan (_carry_forward * _prog_data) {
        // @@ UI configuration is universal: it works in project, target and tools contexts.
        if (_prog_data->f_type <= S_DEFAULT
                && _prog_data->cmd_tokens_count > 0
                && _prog_data->cmd_tokens[0]
                && strcmp((char*)_prog_data->cmd_tokens[0], "ui") == MATCH) {
                if (_prog_data->cmd_tokens_count == 1) {
                        kui_add_line(NOTICE_INFO "UI display mode: " ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET,
                                kui_display_mode_get() == KUI_DISPLAY_FRAME ? "frame" : "continuous");
                } else if (_prog_data->cmd_tokens_count == 3
                        && strcmp((char*)_prog_data->cmd_tokens[1], "mode") == MATCH) {
                        kui_display_mode_t mode;
                        if (strcmp((char*)_prog_data->cmd_tokens[2], "continuous") == MATCH)
                                mode = KUI_DISPLAY_CONTINUOUS;
                        else if (strcmp((char*)_prog_data->cmd_tokens[2], "frame") == MATCH)
                                mode = KUI_DISPLAY_FRAME;
                        else {
                                kui_add_line("< Usage: ui mode [ continuous | frame ]");
                                return;
                        }
                        if (kui_display_mode_set(mode) == 0)
                                kui_add_line(NOTICE_SUCCESS "UI display mode: " ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET,
                                        mode == KUI_DISPLAY_FRAME ? "frame" : "continuous");
                } else {
                        kui_add_line("< Usage: ui mode [ continuous | frame ]");
                }
                return;
        }

        switch (_prog_data->f_type) {
                ///////////////////////////////////////////////////////////////////////////////////////////////////////[Always Growing]
                case S_DEFAULT: { // Figure out which command it is
                        if (strcmp((char*)_prog_data->cmd_tokens[0], "help") == MATCH) {
                                _prog_data->f_type = C_HELP;
                                if (_prog_data->debug_flag == ISTRUE) {f_type_debug_integer_display(_prog_data);}
                                return;
                        }
                        if (strcmp((char*)_prog_data->cmd_tokens[0], "pwd") == MATCH) {
                                _prog_data->f_type = C_PWD;
                                if (_prog_data->debug_flag == ISTRUE) {f_type_debug_integer_display(_prog_data);}
                                return;
                        }
                        if (strcmp((char*)_prog_data->cmd_tokens[0], "new") == MATCH) {
                                _prog_data->f_type = C_NEW;
                                if (_prog_data->debug_flag == ISTRUE) {f_type_debug_integer_display(_prog_data);}
                                return;
                        }
                        if (strcmp((char*)_prog_data->cmd_tokens[0], "unload") == MATCH) {
                                _prog_data->f_type = C_UNLOAD;
                                if (_prog_data->debug_flag == ISTRUE) {f_type_debug_integer_display(_prog_data);}
                                return;
                        }
                        if (strcmp((char*)_prog_data->cmd_tokens[0], "load") == MATCH) {
                                _prog_data->f_type = C_LOAD;
                                if (_prog_data->debug_flag == ISTRUE) {f_type_debug_integer_display(_prog_data);}
                                return;
                        }
                        if (strcmp((char*)_prog_data->cmd_tokens[0], "targets") == MATCH) {
                                _prog_data->f_type = C_TARGETS;
                                if (_prog_data->debug_flag == ISTRUE) {f_type_debug_integer_display(_prog_data);}
                                return;
                        }
                        if (strcmp((char*)_prog_data->cmd_tokens[0], "profile") == MATCH) {
                                _prog_data->f_type = C_PROFILE;
                                if (_prog_data->debug_flag == ISTRUE) {f_type_debug_integer_display(_prog_data);}
                                return;
                        }
                        if (strcmp((char*)_prog_data->cmd_tokens[0], "interfaces") == MATCH) {
                                _prog_data->f_type = C_INTERFACES;
                                if (_prog_data->debug_flag == ISTRUE) {f_type_debug_integer_display(_prog_data);}
                                return;
                        }
                        if (strcmp((char*)_prog_data->cmd_tokens[0], "ping") == MATCH) {
                                _prog_data->f_type = C_PING;
                                if (_prog_data->debug_flag == ISTRUE) {f_type_debug_integer_display(_prog_data);}
                                return;
                        }
                        if (strcmp((char*)_prog_data->cmd_tokens[0], "resolve") == MATCH) {
                                _prog_data->f_type = C_RESOLVE;
                                if (_prog_data->debug_flag == ISTRUE) {f_type_debug_integer_display(_prog_data);}
                                return;
                        }
                        if (strcmp((char*)_prog_data->cmd_tokens[0], "scan") == MATCH) {
                                _prog_data->f_type = C_SCAN;
                                if (_prog_data->debug_flag == ISTRUE) {f_type_debug_integer_display(_prog_data);}
                                return;
                        }
                        if (strcmp((char*)_prog_data->cmd_tokens[0], "book") == MATCH) {
                                _prog_data->f_type = C_BOOK;
                                if (_prog_data->debug_flag == ISTRUE) {f_type_debug_integer_display(_prog_data);}
                                return;
                        }
                        if (strcmp((char*)_prog_data->cmd_tokens[0], "tool") == MATCH) {
                                _prog_data->f_type = C_TOOL;
                                if (_prog_data->debug_flag == ISTRUE) {f_type_debug_integer_display(_prog_data);}
                                return;
                        }
                        // @@ STEP_THREE: Add your command to the ladder ABOVE with respective "data.h" definition ^
                        if (strcmp((char*)_prog_data->cmd_tokens[0], "debug") == MATCH) {
                                _prog_data->f_type = C_DEBUG;
                                if (_prog_data->debug_flag == ISTRUE) {f_type_debug_integer_display(_prog_data);}
                                return;
                        }
                        if (_prog_data->cmd_input[0] != 0x0A) {
                                _prog_data->f_type = C_COMMAND_NOT_FOUND;
                                if (_prog_data->debug_flag == ISTRUE) {f_type_debug_integer_display(_prog_data);}
                                return;
                        } else {
                                _prog_data->f_type = S_DEFAULT;
                                if (_prog_data->debug_flag == ISTRUE) {f_type_debug_integer_display(_prog_data);}
                                return;
                        }
                }
                ///////////////////////////////////////////////////////////////////////////////////////////////////////[Always growing]
                case C_HELP: {
                        help(_prog_data);
                        return;
                }
                ///////////////////////////////////////////////////////////////////////////////////////////////////////[Finished]
                case C_PWD: {
                        kui_add_line(getcwd(NULL, 0));
                        _prog_data->f_type = S_DEFAULT;
                        return;
                }
                ///////////////////////////////////////////////////////////////////////////////////////////////////////[Finished]
                case C_NEW: {
                        if (_prog_data->active_project[0] != 0x00) {
                                _prog_data->unload_from_new = ISTRUE;
                                _prog_data->f_type = C_UNLOAD;
                        } else if (_prog_data->cmd_tokens[1]) {
                                if (_prog_data->new_from_unload == ISTRUE) {
                                        _prog_data->new_from_unload = RESET;
                                }
                                projects_try_new(_prog_data->cmd_tokens[1], _prog_data);
                        } else {
                                kui_add_line("< Please enter a project name:");
                                if (_prog_data->new_from_unload == ISTRUE) {
                                        _prog_data->new_from_unload = RESET;
                                }
                                memset(_prog_data->prompt, 0x00, DOUBLE_BLOCK);
                                strcats((char*)_prog_data->prompt, sizeof(_prog_data->prompt), "[" ANSI_COLOR_CYAN "new" ANSI_COLOR_RESET "]> ");
                                _prog_data->f_type = C_NEW_CONTEXTUAL;
                        }
                        return;
                }
                ///////////////////////////////////////////////////////////////////////////////////////////////////////[Finished]
                case C_NEW_CONTEXTUAL: {
                        memset(_prog_data->prompt, 0x00, DOUBLE_BLOCK);
                        strcats((char*)_prog_data->prompt, sizeof(_prog_data->prompt), "> ");
                        if (_prog_data->cmd_input[0] != 0x0A) {
                                projects_try_new(_prog_data->cmd_tokens[0], _prog_data);
                        } else {
                                kui_add_line("< Cancelled.");
                                _prog_data->f_type = S_DEFAULT;
                        }
                        return;
                }
                ///////////////////////////////////////////////////////////////////////////////////////////////////////[Not Finished]
                case C_UNLOAD: {
                        if (_prog_data->active_project_active_target_context == ISTRUE) {
                                free(_prog_data->active_project_active_target->PETAL);
                                _prog_data->active_project_active_target->PETAL = NULL;
                                kui_add_line(
                                        NOTICE_SUCCESS "Target context "
                                        ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET " unloaded."
                                        , _prog_data->active_project_active_target->TID
                                );
                                _prog_data->active_project_active_target = NULL;
                                _prog_data->active_project_active_target_context = ISFALSE;
                                memset(_prog_data->active_project_active_target_directory, 0x00, MAX_PATH);
                                free(_prog_data->active_project_active_target_directory);
                                _prog_data->active_project_active_target_directory = NULL;
                        }
                        if (_prog_data->active_project_has_targets) {
                                targets_free_nodes(_prog_data->active_project_flower);
                                _prog_data->active_project_target_count = RESET;
                        }
                        if (_prog_data->unload_from_new == ISTRUE) {
                                _prog_data->unload_from_new = RESET;
                                _prog_data->new_from_unload = ISTRUE;
                                _prog_data->f_type = C_NEW;
                        } else if (_prog_data->unload_from_load == ISTRUE) {
                                _prog_data->unload_from_load = RESET;
                                _prog_data->load_from_unload = ISTRUE;
                                _prog_data->f_type = C_LOAD;
                        } else {
                                _prog_data->f_type = S_DEFAULT;
                        }
                        projects_try_unload(_prog_data);
                        return;
                }
                ///////////////////////////////////////////////////////////////////////////////////////////////////////[Not Finished]
                case C_LOAD: {
                        if (_prog_data->active_project[0] != 0x00) {
                                _prog_data->unload_from_load = ISTRUE;
                                _prog_data->f_type = C_UNLOAD;
                        } else if (_prog_data->cmd_tokens[1]) {
                                if (_prog_data->load_from_unload == ISTRUE) {
                                        _prog_data->load_from_unload = RESET;
                                }
                                projects_try_load(_prog_data->cmd_tokens[1], _prog_data);
                        } else {
                                DIR * projects = opendir((char*)KAMI_USER_PROJECTS_DIR);
                                if (!projects) {
                                        kui_add_line("< Error: Failure to find working directory.");
                                        _prog_data->f_type = S_DEFAULT;
                                } else {
                                        if (_prog_data->load_from_unload == ISTRUE) {
                                                _prog_data->load_from_unload = RESET;
                                        }
                                        kui_add_line("< Please type the full name of the project you wish to load:");
                                        kui_add_line("" ANSI_COLOR_MAGENTA);
                                        struct dirent *entries;
                                        int i = 0;
                                        char buffer[BLOCK];
                                        memset(buffer, 0x00, BLOCK);
                                        while ((entries = readdir(projects)) != NULL) {
                                                if (
                                                        (strcmp(entries->d_name, ".") == MATCH)
                                                        || (strcmp(entries->d_name, "..") == MATCH)
                                                        || (strcmp(entries->d_name, "kaminowaku.log") == MATCH)
                                                ) continue;
                                                ++i;
                                                snprintf(buffer, BLOCK, "%d", i);
                                                kui_add_line("%s.)\t" ANSI_COLOR_CYAN "%s" ANSI_COLOR_MAGENTA, buffer, entries->d_name);
                                        }
                                        closedir(projects);
                                        kui_add_line(ANSI_COLOR_RESET "");
                                        if (i == 0) {
                                                kui_add_line("< No projects found.");
                                                _prog_data->f_type = S_DEFAULT;
                                                return;
                                        }
                                }
                                memset(_prog_data->prompt, 0x00, DOUBLE_BLOCK);
                                strcats((char*)_prog_data->prompt, sizeof(_prog_data->prompt), "[" ANSI_COLOR_CYAN "load" ANSI_COLOR_RESET "]> ");
                                _prog_data->f_type = C_LOAD_CONTEXTUAL;
                        }
                        return;
                }
                ///////////////////////////////////////////////////////////////////////////////////////////////////////[Finished]
                case C_LOAD_CONTEXTUAL: {
                        memset(_prog_data->prompt, 0x00, DOUBLE_BLOCK);
                        strcats((char*)_prog_data->prompt, sizeof(_prog_data->prompt), "> ");
                        if (_prog_data->cmd_input[0] != 0x0A) {
                                projects_try_load(_prog_data->cmd_tokens[0], _prog_data);
                        } else {
                                kui_add_line("< Cancelled.");
                                _prog_data->f_type = S_DEFAULT;
                        }
                        return;
                }
                ///////////////////////////////////////////////////////////////////////////////////////////////////////[Not Finished]
                case C_TARGETS: {
                        if (_prog_data->active_project[0] == 0x00) {
                                kui_add_line("< This command requires an active project to be loaded.");
                        } else if (_prog_data->cmd_tokens_count < 2) {
                                targets_print_usage();
                        } else if (strcmp(_prog_data->cmd_tokens[1], "add") == MATCH) {
                                if (_prog_data->cmd_tokens_count < 4) {
                                        targets_print_usage();
                                } else if (
                                        (_prog_data->cmd_tokens_count == 5)
                                        && (strcmp(_prog_data->cmd_tokens[2], "-net") == MATCH)
                                        && (
                                                (strcmp(_prog_data->cmd_tokens[3], "-4") == MATCH)
                                                || (strcmp(_prog_data->cmd_tokens[3], "-6") == MATCH)
                                        )
                                        && (_prog_data->cmd_tokens[4])
                                ) {
                                        if (_prog_data->debug_flag == ISTRUE) {
                                                kui_add_line_and_render(
                                                        "![" ANSI_COLOR_MAGENTA "cmd_scan()" ANSI_COLOR_RESET
                                                        "]> Adding network via " ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET
                                                        " with the value of " ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET,
                                                        _prog_data->cmd_tokens[3],
                                                        _prog_data->cmd_tokens[4]
                                                );
                                        }
                                        targets_add_target(_prog_data);
                                } else if (
                                       ((strcmp(_prog_data->cmd_tokens[2], "-U") == MATCH)
                                        || (strcmp(_prog_data->cmd_tokens[2], "-4") == MATCH)
                                        || (strcmp(_prog_data->cmd_tokens[2], "-6") == MATCH)
                                        || (strcmp(_prog_data->cmd_tokens[2], "-M") == MATCH))
                                        && (_prog_data->cmd_tokens[3])
                                ) {
                                        if (_prog_data->debug_flag == ISTRUE) {
                                                kui_add_line_and_render(
                                                        "![" ANSI_COLOR_MAGENTA "cmd_scan()" ANSI_COLOR_RESET
                                                        "]> Adding target via " ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET
                                                        " with the value of " ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET,
                                                        _prog_data->cmd_tokens[2],
                                                        _prog_data->cmd_tokens[3]
                                                );
                                        }
                                        targets_add_target(_prog_data);
                                } else {
                                        targets_print_usage();
                                }
                        } else if (
                                (strcmp(_prog_data->cmd_tokens[1], "del") == MATCH)
                                || (strcmp(_prog_data->cmd_tokens[1], "delete") == MATCH)
                        ) {
                                if (_prog_data->cmd_tokens_count < 3) {
                                        targets_print_usage();
                                } else if (
                                        _prog_data->cmd_tokens_count == 3
                                        && strcmp(_prog_data->cmd_tokens[2], "-n") == MATCH
                                ) {
                                        targets_del_no_neighbor(_prog_data);
                                } else if (
                                       ((strcmp(_prog_data->cmd_tokens[2], "-U") == MATCH)
                                        || (strcmp(_prog_data->cmd_tokens[2], "-4") == MATCH)
                                        || (strcmp(_prog_data->cmd_tokens[2], "-6") == MATCH)
                                        || (strcmp(_prog_data->cmd_tokens[2], "-M") == MATCH))
                                        && (_prog_data->cmd_tokens[3])
                                ) {
                                        if (_prog_data->debug_flag == ISTRUE) {
                                                kui_add_line_and_render(
                                                        "![" ANSI_COLOR_MAGENTA "cmd_scan()" ANSI_COLOR_RESET
                                                        "]> Deleting target via " ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET
                                                        " with the value of " ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET,
                                                        _prog_data->cmd_tokens[2],
                                                        _prog_data->cmd_tokens[3]
                                                );
                                        }
                                        targets_del_target(_prog_data, MODE_IDENTIFIER);
                                } else if (_prog_data->cmd_tokens_count == 3) {
                                        targets_del_target(_prog_data, MODE_TID);
                                } else {
                                        targets_print_usage();
                                }
                        } else if (strcmp(_prog_data->cmd_tokens[1], "display") == MATCH) {
                                if (
                                        _prog_data->cmd_tokens_count == 2
                                        || (
                                                _prog_data->cmd_tokens_count == 3
                                                && (
                                                        strcmp(_prog_data->cmd_tokens[2], "-d") == MATCH
                                                        || strcmp(_prog_data->cmd_tokens[2], "-o") == MATCH
                                                )
                                        )
                                        || (
                                                _prog_data->cmd_tokens_count == 4
                                                && (
                                                        strcmp(_prog_data->cmd_tokens[2], "-p") == MATCH
                                                        || strcmp(_prog_data->cmd_tokens[2], "-b") == MATCH
                                                )
                                        )
                                ) {
                                        targets_display_from_project(_prog_data);
                                } else {
                                        kui_add_line("< Usage: targets display [ -d | -o | -p <port> | -b <port> ]");
                                }
                        } else if (
                                (strcmp(_prog_data->cmd_tokens[1], "-U") == MATCH)
                                || (strcmp(_prog_data->cmd_tokens[1], "-4") == MATCH)
                                || (strcmp(_prog_data->cmd_tokens[1], "-6") == MATCH)
                                || (strcmp(_prog_data->cmd_tokens[1], "-M") == MATCH)
                        ) {
                                targets_context(_prog_data, MODE_IDENTIFIER);
                        } else if (_prog_data->cmd_tokens[1]) {
                                targets_context(_prog_data, MODE_TID);
                        } else {
                                targets_print_usage();
                        }
                        if (_prog_data->f_type != C_TARGETS_CONTEXTUAL) _prog_data->f_type = S_DEFAULT;
                        return;
                }
                ///////////////////////////////////////////////////////////////////////////////////////////////////////[Not Finished]
                case C_TARGETS_CONTEXTUAL: {
                        if (strcmp(_prog_data->cmd_tokens[0], "back") == MATCH) {
                                memset(_prog_data->prompt, 0x00, DOUBLE_BLOCK);
                                strcats((char*)_prog_data->prompt, sizeof(_prog_data->prompt), "> ");
                                free(_prog_data->active_project_active_target->PETAL);
                                _prog_data->active_project_active_target->PETAL = NULL;
                                kui_add_line(
                                        NOTICE_SUCCESS "Target context "
                                        ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET " unloaded.",
                                        _prog_data->active_project_active_target->TID
                                );
                                _prog_data->active_project_active_target = NULL;
                                _prog_data->active_project_active_target_context = ISFALSE;
                                memset(_prog_data->active_project_active_target_directory, 0x00, MAX_PATH);
                                free(_prog_data->active_project_active_target_directory);
                                _prog_data->active_project_active_target_directory = NULL;
                                _prog_data->help_caller_state_manager = S_DEFAULT;
                                _prog_data->f_type = S_DEFAULT;
                        } else if (strcmp(_prog_data->cmd_tokens[0], "help") == MATCH) {
                                _prog_data->f_type = C_HELP;
                                _prog_data->help_caller_state_manager = C_TARGETS_CONTEXTUAL;
                                return;
                        } else if (strcmp(_prog_data->cmd_tokens[0], "set") == MATCH) {
                                targets_set_identifier(_prog_data);
                        } else if (
                                (strcmp(_prog_data->cmd_tokens[0], "display") == MATCH)
                                || (strcmp(_prog_data->cmd_tokens[0], "dt") == MATCH)
                        ) {
                                if (_prog_data->cmd_tokens_count == 1) {
                                        targets_display_from_context(_prog_data);
                                } else {
                                        kui_add_line("< Usage: display");
                                        kui_add_line(
                                                NOTICE_INFO "Aliases: " ANSI_COLOR_MAGENTA "dt / d / dd" ANSI_COLOR_RESET
                                        );
                                }
                        } else if (strcmp(_prog_data->cmd_tokens[0], "ping") == MATCH) {
                                ksping_single(_prog_data);
                        } else if (strcmp(_prog_data->cmd_tokens[0], "resolve") == MATCH) {
                                kresolve_single(_prog_data);
                        } else if (strcmp(_prog_data->cmd_tokens[0], "scan") == MATCH) {
                                kportscan_run(_prog_data);
                        } else if (strcmp(_prog_data->cmd_tokens[0], "book") == MATCH) {
                                books_try_run(_prog_data);
                        } else if (strcmp(_prog_data->cmd_tokens[0], "tool") == MATCH) {
                                _prog_data->f_type = C_TOOL;
                                return;
                        } else {
                                kui_add_line("< Unrecognized target command.");
                        }
                        return;
                }
                ///////////////////////////////////////////////////////////////////////////////////////////////////////[Not Finished]
                case C_PROFILE: {
                        if (
                                _prog_data->cmd_tokens_count == 1
                                || (
                                        _prog_data->cmd_tokens_count == 2
                                        && strcmp((char*)_prog_data->cmd_tokens[1], "display") == MATCH
                                )
                        ) {
                                profile_display_global(_prog_data);
                        } else if (strcmp((char*)_prog_data->cmd_tokens[1], "load") == MATCH) {
                                if (_prog_data->cmd_tokens_count != 3) {
                                        memset(_prog_data->prompt, 0x00, DOUBLE_BLOCK);
                                        strcats(
                                                (char*)_prog_data->prompt,
                                                sizeof(_prog_data->prompt),
                                                "[" ANSI_COLOR_CYAN "profile" ANSI_COLOR_RESET "]> "
                                        );
                                        profile_list_available();
                                        _prog_data->f_type = C_PROFILE_CONTEXTUAL;
                                        return;
                                } else {
                                        profile_try_load(_prog_data->cmd_tokens[2], _prog_data);
                                }
                        } else if (strcmp((char*)_prog_data->cmd_tokens[1], "set") == MATCH) {
                                if (_prog_data->cmd_tokens_count != 4) {
                                        profile_print_usage();
                                } else {
                                        profile_try_set(
                                                _prog_data->cmd_tokens[2],
                                                _prog_data->cmd_tokens[3],
                                                _prog_data
                                        );
                                }
                        } else if (strcmp((char*)_prog_data->cmd_tokens[1], "save") == MATCH) {
                                if (_prog_data->cmd_tokens_count == 3) {
                                        profile_try_save(
                                                _prog_data->cmd_tokens[2],
                                                ISFALSE,
                                                _prog_data
                                        );
                                } else if (
                                        _prog_data->cmd_tokens_count == 4
                                        && strcmp((char*)_prog_data->cmd_tokens[3], "overwrite") == MATCH
                                ) {
                                        profile_try_save(
                                                _prog_data->cmd_tokens[2],
                                                ISTRUE,
                                                _prog_data
                                        );
                                } else {
                                        profile_print_usage();
                                }
                        } else {
                                profile_print_usage();
                        }
                        _prog_data->f_type = S_DEFAULT;
                        return;
                }
                ///////////////////////////////////////////////////////////////////////////////////////////////////////[Not Finished]
                case C_PROFILE_CONTEXTUAL: {
                        memset(_prog_data->prompt, 0x00, DOUBLE_BLOCK);
                        strcats((char*)_prog_data->prompt, sizeof(_prog_data->prompt), "> ");
                        if (_prog_data->cmd_input[0] != 0x0A) {
                                profile_try_load(_prog_data->cmd_tokens[0], _prog_data);
                                _prog_data->f_type = S_DEFAULT;
                        } else {
                                kui_add_line("< Cancelled.");
                                profile_print_load_usage();
                                _prog_data->f_type = S_DEFAULT;
                        }
                        return;
                }
                ///////////////////////////////////////////////////////////////////////////////////////////////////////[Not Finished]
                case C_INTERFACES: {
                        interfaces();
                        _prog_data->f_type = S_DEFAULT;
                        return;
                }
                ///////////////////////////////////////////////////////////////////////////////////////////////////////[Finished]
                case C_PING: {
                        if (_prog_data->active_project[0] == 0x00) {
                                kui_add_line(NOTICE_WARNING "No active project.");
                                _prog_data->f_type = S_DEFAULT;
                                return;
                        }
                        if (_prog_data->active_project_has_targets == ISFALSE) {
                                kui_add_line(NOTICE_WARNING "No targets.");
                                _prog_data->f_type = S_DEFAULT;
                                return;
                        }
                        if (_prog_data->cmd_tokens_count != 2) {
                                kui_add_line("< Usage: ping [ -4 | -6 ]");
                                _prog_data->f_type = S_DEFAULT;
                                return;
                        }
                        if (
                                (strcmp((char*)_prog_data->cmd_tokens[1], "-4") == MATCH)
                                || (strcmp((char*)_prog_data->cmd_tokens[1], "-6") == MATCH)
                        ) {
                                ksping_multi(_prog_data);
                        } else {
                                kui_add_line("< Usage: ping [ -4 | -6 ]");
                        }
                        _prog_data->f_type = S_DEFAULT;
                        return;
                }
                ///////////////////////////////////////////////////////////////////////////////////////////////////////[Finished]
                case C_RESOLVE: {
                        if (_prog_data->active_project[0] == 0x00) {
                                kui_add_line(NOTICE_WARNING "No active project.");
                                _prog_data->f_type = S_DEFAULT;
                                return;
                        }
                        if (_prog_data->active_project_has_targets == ISFALSE) {
                                kui_add_line(NOTICE_WARNING "No targets.");
                                _prog_data->f_type = S_DEFAULT;
                                return;
                        }
                        if (_prog_data->cmd_tokens_count != 1) {
                                kui_add_line("< Usage: resolve");
                                _prog_data->f_type = S_DEFAULT;
                                return;
                        }
                        kresolve_multi(_prog_data);
                        _prog_data->f_type = S_DEFAULT;
                        return;
                }
                ///////////////////////////////////////////////////////////////////////////////////////////////////////[First Pass]
                case C_SCAN: {
                        if (_prog_data->active_project[0] == 0x00) {
                                kui_add_line(NOTICE_WARNING "No active project.");
                                _prog_data->f_type = S_DEFAULT;
                                return;
                        }
                        if (_prog_data->active_project_has_targets == ISFALSE) {
                                kui_add_line(NOTICE_WARNING "No targets.");
                                _prog_data->f_type = S_DEFAULT;
                                return;
                        }
                        kportscan_run(_prog_data);
                        _prog_data->f_type = S_DEFAULT;
                        return;
                }
                ///////////////////////////////////////////////////////////////////////////////////////////////////////[First Pass]
                case C_BOOK: { // Execute user defined scan scripts
                        books_try_run(_prog_data);
                        _prog_data->f_type = S_DEFAULT;
                        return;
                }
                ///////////////////////////////////////////////////////////////////////////////////////////////////////[First Pass]
                case C_TOOL: { // Manage external tools or enter tool execution context
                        f_type_t RETURN_STATE = (
                                _prog_data->active_project_active_target_context == ISTRUE
                        ) ? C_TARGETS_CONTEXTUAL : S_DEFAULT;

                        if (_prog_data->cmd_tokens_count == 1) {
                                if (
                                        _prog_data->active_project_active_target_context != ISTRUE
                                        || !_prog_data->active_project_active_target
                                ) {
                                        kui_add_line(
                                                NOTICE_WARNING
                                                "Tool execution requires an active target context."
                                        );
                                        _prog_data->f_type = RETURN_STATE;
                                        return;
                                }

                                memset(_prog_data->prompt, 0x00, DOUBLE_BLOCK);
                                strcats(
                                        (char*)_prog_data->prompt,
                                        sizeof(_prog_data->prompt),
                                        "[" ANSI_COLOR_CYAN "tools" ANSI_COLOR_RESET "]> "
                                );
                                _prog_data->help_caller_state_manager = C_TOOL_CONTEXTUAL;
                                _prog_data->f_type = C_TOOL_CONTEXTUAL;
                                kui_add_line(
                                        NOTICE_INFO
                                        "External tool context active for target "
                                        ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET ".",
                                        _prog_data->active_project_active_target->TID
                                );
                                return;
                        }

                        if (
                                (_prog_data->cmd_tokens_count == 3)
                                && (strcmp((char*)_prog_data->cmd_tokens[1], "add") == MATCH)
                        ) {
                                tools_add(_prog_data);
                        } else if (
                                (_prog_data->cmd_tokens_count == 2)
                                && (strcmp((char*)_prog_data->cmd_tokens[1], "list") == MATCH)
                        ) {
                                tools_list();
                        } else if (
                                (_prog_data->cmd_tokens_count == 3)
                                && (strcmp((char*)_prog_data->cmd_tokens[1], "del") == MATCH)
                        ) {
                                tools_del(_prog_data);
                        } else {
                                tools_print_usage();
                        }

                        _prog_data->f_type = RETURN_STATE;
                        return;
                }
                ///////////////////////////////////////////////////////////////////////////////////////////////////////[First Pass]
                case C_TOOL_CONTEXTUAL: { // External tool execution context
                        if (strcmp((char*)_prog_data->cmd_tokens[0], "back") == MATCH) {
                                if (
                                        _prog_data->active_project_active_target_context == ISTRUE
                                        && _prog_data->active_project_active_target
                                ) {
                                        memset(_prog_data->prompt, 0x00, DOUBLE_BLOCK);
                                        strcats(
                                                (char*)_prog_data->prompt,
                                                sizeof(_prog_data->prompt),
                                                "[" ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET "]> ",
                                                _prog_data->active_project_active_target->TID
                                        );
                                        _prog_data->help_caller_state_manager = C_TARGETS_CONTEXTUAL;
                                        _prog_data->f_type = C_TARGETS_CONTEXTUAL;
                                        kui_add_line(NOTICE_INFO "External tool context exited.");
                                } else {
                                        memset(_prog_data->prompt, 0x00, DOUBLE_BLOCK);
                                        strcats((char*)_prog_data->prompt, sizeof(_prog_data->prompt), "> ");
                                        _prog_data->help_caller_state_manager = S_DEFAULT;
                                        _prog_data->f_type = S_DEFAULT;
                                        kui_add_line(
                                                NOTICE_WARNING
                                                "Target context is no longer available; returned to project context."
                                        );
                                }
                                return;
                        }

                        if (strcmp((char*)_prog_data->cmd_tokens[0], "help") == MATCH) {
                                _prog_data->help_caller_state_manager = C_TOOL_CONTEXTUAL;
                                _prog_data->f_type = C_HELP;
                                return;
                        }

                        if (strcmp((char*)_prog_data->cmd_tokens[0], "list") == MATCH) {
                                if (_prog_data->cmd_tokens_count == 1) {
                                        tools_list();
                                } else {
                                        kui_add_line("< Usage: list");
                                }
                                return;
                        }

                        if (strcmp((char*)_prog_data->cmd_tokens[0], "add") == MATCH) {
                                if (_prog_data->cmd_tokens_count == 2) {
                                        tools_add_context(_prog_data);
                                } else {
                                        kui_add_line(
                                                "< Usage: add " ANSI_COLOR_CYAN "<absolute-path>" ANSI_COLOR_RESET
                                        );
                                }
                                return;
                        }

                        if (strcmp((char*)_prog_data->cmd_tokens[0], "del") == MATCH) {
                                if (_prog_data->cmd_tokens_count == 2) {
                                        tools_del_context(_prog_data);
                                } else {
                                        kui_add_line(
                                                "< Usage: del " ANSI_COLOR_CYAN "<tool-name>" ANSI_COLOR_RESET
                                        );
                                }
                                return;
                        }

                        tool_exec_run(_prog_data);
                        return;
                }
                ///////////////////////////////////////////////////////////////////////////////////////////////////////[Finished]
                case C_COMMAND_NOT_FOUND: {
                        kui_add_line("< Command not found.");
                        _prog_data->f_type = S_DEFAULT;
                        return;
                }
                ///////////////////////////////////////////////////////////////////////////////////////////////////////[Finished?]
                case C_DEBUG: {
                        if (_prog_data->cmd_tokens[1]) {
                                if (strcmp((char*)_prog_data->cmd_tokens[1], "on") == MATCH) {
                                        _prog_data->debug_flag = ISTRUE;
                                        _prog_data->render_mode = RENDER_SCROLLING;
                                } else if (strcmp((char*)_prog_data->cmd_tokens[1], "off") == MATCH) {
                                        memset(_prog_data->prompt, 0x00, DOUBLE_BLOCK);
                                        strcats((char*)_prog_data->prompt, sizeof(_prog_data->prompt), "> ");
                                        _prog_data->debug_flag = ISFALSE;
                                        _prog_data->render_mode = RENDER_NORMAL;
                                } else {
                                        kui_add_line("< Usage: debug [ on | off ]");
                                }
                        } else {
                                kui_add_line("< Usage: debug [ on | off ]");
                        }
                        _prog_data->f_type = S_DEFAULT;
                        return;
                }
        }
}
