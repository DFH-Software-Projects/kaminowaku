// Copyright 2026 Jamison A. Drapeau
#include "kaminowaku.h"
#include "cmd_scan.h"
#include "gprofile.h"
#include "helpers.h"
#include "banner.h"

// Multi-Linked
#include "kio.h"
#include "kui.h"
#include <pwd.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

// @@ Bad exit notifier
static void FAILURE(void) {
        printf(NOTICE_ERROR); fflush(stdout);
        perror(NULL);
        printf(NOTICE_ERROR "Program failure, please check the logs.\n"); fflush(stdout);
        printf(NOTICE_INFO "If this problem persists back up critical data and re-install.\n"); fflush(stdout);
}

// @@ Macro Stuff | INPUT COMMAND ALIASES IN THE TABLE BELOW
typedef struct {
        const char *cmd_alias;
        const char *cmd_expansion;
} cmd_alias_t;
static const cmd_alias_t CMD_ALIAS_ARRAY[] = {
        {"h", "help"},
        {"n", "new"},
        {"l", "load"},
        {"u", "unload"},
        {"d", "targets display"},
        {"dd", "targets display -d"},
        {"a", "targets add"},
        {"t", "targets"},
        {"p", "profile"},
        {"s", "set"},
        {"b", "book"},
        {"i", "interfaces"},
        {"r", "resolve"},
        { NULL, NULL}
};
static int cmd_alias_boundary(unsigned char c) {
        return (
                (c == 0x00) ||  // string terminator
                (c == 0x0A) ||  // newline, mostly pre-sanitize paranoia
                (c == 0x20)     // space
        );
}

// @@ Macro Handler (AI Generated, human modified)
static void CANONICALIZE_CMD_ALIAS(_carry_forward * _prog_data) {
        unsigned char *cmd_input_reference = _prog_data->cmd_input;
        unsigned char expansion_buffer[INPUT_BLOCK];
        memset(expansion_buffer, 0x00, INPUT_BLOCK);
        while (*cmd_input_reference == 0x20) {
                cmd_input_reference++;
        }

        // @@ Display aliases are context-sensitive.
        // Project: d -> targets display, dd -> targets display -d.
        // Target:  d/dd -> display, matching display/dt complete target output.
        if (_prog_data->active_project_active_target_context == ISTRUE) {
                const char * contextual_alias = NULL;
                size_t alias_length = 0;

                if (
                        strncmp((char*)cmd_input_reference, "dd", 2) == MATCH
                        && cmd_alias_boundary(cmd_input_reference[2])
                ) {
                        contextual_alias = "display";
                        alias_length = 2;
                } else if (
                        strncmp((char*)cmd_input_reference, "d", 1) == MATCH
                        && cmd_alias_boundary(cmd_input_reference[1])
                ) {
                        contextual_alias = "display";
                        alias_length = 1;
                }

                if (contextual_alias) {
                        snprintf(
                                (char*)expansion_buffer,
                                sizeof(expansion_buffer),
                                "%s%s",
                                contextual_alias,
                                cmd_input_reference + alias_length
                        );
                        memset(_prog_data->cmd_input, 0x00, INPUT_BLOCK);
                        snprintf(
                                (char*)_prog_data->cmd_input,
                                INPUT_BLOCK,
                                "%s",
                                expansion_buffer
                        );
                        return;
                }
        }

        for (int i = 0; CMD_ALIAS_ARRAY[i].cmd_alias != NULL; i++) {
                const char *cmd_alias     = CMD_ALIAS_ARRAY[i].cmd_alias;
                const char *cmd_expansion = CMD_ALIAS_ARRAY[i].cmd_expansion;
                size_t cmd_alias_len      = strlen(cmd_alias);
                if (
                        (strncmp((char*)cmd_input_reference, cmd_alias, cmd_alias_len) == MATCH)
                        &&
                        cmd_alias_boundary(cmd_input_reference[cmd_alias_len])
                ) {
                        snprintf(
                                (char*)expansion_buffer,
                                sizeof(expansion_buffer),
                                "%s%s",
                                cmd_expansion,
                                cmd_input_reference + cmd_alias_len
                        );
                        memset(_prog_data->cmd_input, 0x00, INPUT_BLOCK);
                        snprintf(
                                (char*)_prog_data->cmd_input,
                                INPUT_BLOCK,
                                "%s",
                                expansion_buffer
                        );
                        return;
                }
        }
        return;
}

// @@ Input Scrubbing
static void SANITIZE_INPUT(_carry_forward * _prog_data) {
        // Scrub Input
        int i = 0;
        int first_good = 0;
        //_prog_data->cmd_input[INPUT_BLOCK] = 0x00;
        if (_prog_data->cmd_input[0] != 0x0A) {
                for ( i = INPUT_BLOCK - 1; i != -1; --i) {
                        if (
                                (
                                        (_prog_data->cmd_input[i] < 48)
                                        || // Not a Number
                                        (_prog_data->cmd_input[i] > 57)
                                ) && (
                                        (_prog_data->cmd_input[i] < 65)
                                        || // Not an uppercase Letter
                                        (_prog_data->cmd_input[i] > 90)
                                ) && (
                                        (_prog_data->cmd_input[i] < 97)
                                        || // Not a lowercase Letter
                                        (_prog_data->cmd_input[i] > 122)
                                ) && (  // Not one of these specific ascii characters
                                        (_prog_data->cmd_input[i] != 0x2D) /* - */
                                        &&
                                        (_prog_data->cmd_input[i] != 0x2E) /* . */
                                        &&
                                        (_prog_data->cmd_input[i] != 0x2F) /* / */
                                        &&
                                        (_prog_data->cmd_input[i] != 0x20) /*   */
                                        &&
                                        (_prog_data->cmd_input[i] != 0x5F) /* _ */
                                        &&
                                        (_prog_data->cmd_input[i] != 0x3A) /* : */
                                        &&
                                        (_prog_data->cmd_input[i] != 0x22) /* " */
                                        &&
                                        (_prog_data->cmd_input[i] != 0x21) /* ! */
                                        &&
                                        (_prog_data->cmd_input[i] != 0x23) /* # */
                                        &&
                                        (_prog_data->cmd_input[i] != 0x24) /* $ */
                                        &&
                                        (_prog_data->cmd_input[i] != 0x25) /* % */
                                        &&
                                        (_prog_data->cmd_input[i] != 0x26) /* & */
                                        &&
                                        (_prog_data->cmd_input[i] != 0x27) /* ' */
                                        &&
                                        (_prog_data->cmd_input[i] != 0x28) /* ( */
                                        &&
                                        (_prog_data->cmd_input[i] != 0x29) /* ) */
                                        &&
                                        (_prog_data->cmd_input[i] != 0x2A) /* * */
                                        &&
                                        (_prog_data->cmd_input[i] != 0x2B) /* + */
                                        &&
                                        (_prog_data->cmd_input[i] != 0x2C) /* , */
                                        &&
                                        (_prog_data->cmd_input[i] != 0x3B) /* ; */
                                        &&
                                        (_prog_data->cmd_input[i] != 0x3C) /* < */
                                        &&
                                        (_prog_data->cmd_input[i] != 0x3D) /* = */
                                        &&
                                        (_prog_data->cmd_input[i] != 0x3E) /* > */
                                        &&
                                        (_prog_data->cmd_input[i] != 0x3F) /* ? */
                                        &&
                                        (_prog_data->cmd_input[i] != 0x40) /* @ */
                                        &&
                                        (_prog_data->cmd_input[i] != 0x5B) /* [ */
                                        &&
                                        (_prog_data->cmd_input[i] != 0x5C) /* \ */
                                        &&
                                        (_prog_data->cmd_input[i] != 0x5D) /* ] */
                                        &&
                                        (_prog_data->cmd_input[i] != 0x5E) /* ^ */
                                        &&
                                        (_prog_data->cmd_input[i] != 0x60) /* ` */
                                        &&
                                        (_prog_data->cmd_input[i] != 0x7B) /* { */
                                        &&
                                        (_prog_data->cmd_input[i] != 0x7C) /* | */
                                        &&
                                        (_prog_data->cmd_input[i] != 0x7D) /* } */
                                        &&
                                        (_prog_data->cmd_input[i] != 0x7E) /* ~ */
                                )
                        ) {
                                     if (first_good == 0) {_prog_data->cmd_input[i] = 0x00;}
                                else if (first_good == 1) {_prog_data->cmd_input[i] = 0x20;}
                        } else if (first_good == 0) {first_good = 1;}
                }
        }
}

// @@ Tokenization for command inputs
static void input_tokenization(_carry_forward * _prog_data) {
        
        // Copy out input for a modifiable string
        unsigned char *_COPY = strdup((char*)_prog_data->cmd_input);
        
        // Handle failure of copy
        if (!_COPY) {
                return;
        }

        // Create the save pointers and the tokens
        unsigned char *_SAVEPTR;
        unsigned char *_TOKEN = strtok_r((char*)_COPY, " ", &_SAVEPTR);

        // Define capacity and size
        size_t _CAPACITY        = TOKENS_INITIAL_CAPACITY;
        size_t _SIZE            = TOKENS_INITIAL_SIZE;

        // WARN: Begin malloc
        _prog_data->cmd_tokens = malloc(_CAPACITY * sizeof(unsigned char*));
        if (!_prog_data->cmd_tokens) {
                free(_COPY);
                _prog_data->cmd_tokens = NULL;
                return;
        }

        // Prevent garbage by zeroing the array
        for (size_t i = 0; i < _CAPACITY; i++) _prog_data->cmd_tokens[i] = NULL;


        // WARN: Start stacking tokens up
        while (_TOKEN != NULL) {

                // Check limits of allocation
                if (_SIZE >= _CAPACITY) {

                        // Increase Capacity
                        _CAPACITY *= TOKENS_INITIAL_CAPACITY;

                        // Add memory
                        unsigned char **_REALLOC_BUFFER = realloc(_prog_data->cmd_tokens, _CAPACITY * sizeof(unsigned char*));

                        // Handle realloc failure
                        if (!_REALLOC_BUFFER) {
                                for (size_t s = 0; s < _SIZE; s++) free(_prog_data->cmd_tokens[s]);
                                free(_prog_data->cmd_tokens);
                                free(_COPY);
                                _prog_data->cmd_tokens = NULL;
                                return;
                        }

                        // Zero slots to avoid garbage
                        for (size_t s = _SIZE; s < _CAPACITY; s++) _REALLOC_BUFFER[s] = NULL;

                        // Pass over expansion_buffer updating memory allocation
                        _prog_data->cmd_tokens = _REALLOC_BUFFER;
                }

                // Copy each token
                _prog_data->cmd_tokens[_SIZE] = strdup(_TOKEN);

                // Check for bad copy 
                if (!_prog_data->cmd_tokens[_SIZE]) {
                        for (size_t s = 0; s < _SIZE; s++) free(_prog_data->cmd_tokens[s]);
                        free(_prog_data->cmd_tokens);
                        free(_COPY);
                        _prog_data->cmd_tokens = NULL;
                        return;
                }

                // Add to the size
                _SIZE++;

                // Iterate to next token
                _TOKEN = strtok_r(NULL, " ", &_SAVEPTR);
        }

        // Free the copy, return token count, return tokens
        free(_COPY);
        _prog_data->cmd_tokens_count = _SIZE;
        return;
}
        
// @@ Free allocated memory for the input tokens
static void input_tokens_free(_carry_forward * _prog_data) {
        size_t s = TOKENS_INITIAL_SIZE;
        while (s < _prog_data->cmd_tokens_count) {
                if (_prog_data->cmd_tokens[s]) {
                        free(_prog_data->cmd_tokens[s]);
                        _prog_data->cmd_tokens[s] = NULL;
                }
                ++s;
        }
        free(_prog_data->cmd_tokens);
        _prog_data->cmd_tokens = NULL;
        _prog_data->cmd_tokens_count = TOKENS_INITIAL_SIZE;
        return;
}

/*
######################################################################################################################################################
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
######################################################################################################################################################

DESCRIPTION

        This function is the main prompt and banner generator for the program.
        It will be called during both input and output regarding user commands.

######################################################################################################################################################
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
######################################################################################################################################################
*/

// @@ Core looping code
int kaminowaku (_carry_forward * _prog_data) {

        // @@ Screen nuke
        //printf("\033\143");

        // C99 for loop error bypass ints
        int c = 0;

        // Scoped file information for program directory tree validation
        struct stat info;

        /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        //
        //      INITIALIZATION OF CORE DATA
        //
        // Catch first instance here, i.e. f_type = 0
        // @@ This is where startup logic is defined, NOT IN MAIN.
        /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        if (_prog_data->f_type == S_STARTUP) {

                // Set initial status banner and print the ascii art on first boot
                printf(
                        ANSI_COLOR_MAGENTA "[" ANSI_COLOR_RESET "KAMI NO WAKU " ANSI_COLOR_CYAN VERSION ANSI_COLOR_RESET
                        ANSI_COLOR_MAGENTA "]\n" ANSI_COLOR_RESET
                        RGB_COLOR_DARK_MAGENTA     "┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓" ANSI_COLOR_RESET "\n"
                        RGB_COLOR_DARK_MAGENTA     "┃"ANSI_COLOR_RESET
                        ANSI_COLOR_CYAN            "⠄⣾⣿⡇⢸⣿⣿⣿⠄⠈⣿⣿⣿⣿⠈⣿⡇⢹⣿⣿⣿⡇⡇⢸⣿⣿⡇⣿⣿⣿" ANSI_COLOR_RESET
                        RGB_COLOR_DARK_MAGENTA     "┃"ANSI_COLOR_RESET "\n"
                        RGB_COLOR_DARK_MAGENTA     "┃"ANSI_COLOR_RESET
                        ANSI_COLOR_CYAN            "⢠⣿⣿⡇⢸⣿⣿⣿⡇⠄⢹⣿⣿⣿⡀⣿⣧⢸⣿⣿⣿⠁⡇⢸⣿⣿⠁⣿⣿⣿" ANSI_COLOR_RESET
                        RGB_COLOR_DARK_MAGENTA     "┃"ANSI_COLOR_RESET "\n"
                        RGB_COLOR_DARK_MAGENTA     "┃"ANSI_COLOR_RESET
                        ANSI_COLOR_CYAN            "⢸⣿⣿⡇⠸⣿⣿⣿⣿⡄⠈⢿⣿⣿⡇⢸⣿⡀⣿⣿⡿⠸⡇⣸⣿⣿⠄⣿⣿⣿" ANSI_COLOR_RESET
                        RGB_COLOR_DARK_MAGENTA     "┃"ANSI_COLOR_RESET "\n"
                        RGB_COLOR_DARK_MAGENTA     "┃"ANSI_COLOR_RESET
                        ANSI_COLOR_CYAN            "⢸⣿⡿⠷⠄⠿⠿⠿⠟⠓⠰⠘⠿⣿⣿⡈⣿⡇⢹⡟⠰⠦⠁⠈⠉⠋⠄⠻⢿⣿" ANSI_COLOR_RESET
                        RGB_COLOR_DARK_MAGENTA     "┃"ANSI_COLOR_RESET "\n"
                        RGB_COLOR_DARK_MAGENTA     "┃"ANSI_COLOR_RESET
                        ANSI_COLOR_CYAN            "⢨⡑⠶⡏⠛⠐⠋⠓⠲⠶⣭⣤⣴⣦⣭⣥⣮⣾⣬⣴⡮⠝⠒⠂⠂⠘⠉⠿⠖⣬" ANSI_COLOR_RESET
                        RGB_COLOR_DARK_MAGENTA     "┃"ANSI_COLOR_RESET "\n"
                        RGB_COLOR_DARK_MAGENTA     "┃"ANSI_COLOR_RESET
                        ANSI_COLOR_CYAN            "⠈⠉⠄⡀⠄⣀⣀⣀⣀⠈⢛⣿⣿⣿⣿⣿⣿⣿⣿⣟⠁⣀⣤⣤⣠⡀⠄⡀⠈⠁" ANSI_COLOR_RESET
                        RGB_COLOR_DARK_MAGENTA     "┃"ANSI_COLOR_RESET "\n"
                        RGB_COLOR_DARK_MAGENTA     "┃"ANSI_COLOR_RESET
                        ANSI_COLOR_CYAN            "⠄⠠⣾⡀⣾⣿⣧⣼⣿⡿⢠⣿⣿⣿⣿⣿⣿⣿⣿⣧⣼⣿⣧⣼⣿⣿⢀⣿⡇⠄" ANSI_COLOR_RESET
                        RGB_COLOR_DARK_MAGENTA     "┃"ANSI_COLOR_RESET "\n"
                        RGB_COLOR_DARK_MAGENTA     "┃"ANSI_COLOR_RESET
                        ANSI_COLOR_CYAN            "⡀⠄⠻⣷⡘⢿⣿⣿⡿⢣⣾⣿⣿⣿⣿⣿⣿⣿⣿⣿⣜⢿⣿⣿⡿⢃⣾⠟⢁⠈" ANSI_COLOR_RESET
                        RGB_COLOR_DARK_MAGENTA     "┃"ANSI_COLOR_RESET "\n"
                        RGB_COLOR_DARK_MAGENTA     "┃"ANSI_COLOR_RESET
                        ANSI_COLOR_CYAN            "⢃⢻⣶⣬⣿⣶⣬⣥⣶⣿⣿⣿⣿⣿⣿⢿⣿⣿⣿⣿⣿⣷⣶⣶⣾⣿⣷⣾⣾⢣" ANSI_COLOR_RESET
                        RGB_COLOR_DARK_MAGENTA     "┃"ANSI_COLOR_RESET "\n"
                        RGB_COLOR_DARK_MAGENTA     "┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛" ANSI_COLOR_RESET "\n"
                );

                // Check for root permissions
                if (getuid() != 0) {
                        printf(NOTICE_ERROR "This program requires root permissions.\n");
                        printf(NOTICE_INFO  "Please restart using sudo or the root user account.\n");
                        return 0;
                }

                // Get user's home directory
                struct passwd * pw = getpwuid(getuid());
                if (pw == NULL) {
                        FAILURE();
                        return 0;
                } else {
                        printf(NOTICE_SUCCESS "Found %s's home.\n", pw->pw_name);
                        strcats(_prog_data->wd, sizeof(_prog_data->wd), pw->pw_dir);
                        strcats(_prog_data->wd, sizeof(_prog_data->wd), "/.kaminowaku/");
                }

                // Create "kami root" directory
                if (stat(_prog_data->wd, &info) == NORMAL) {
                        if (S_ISDIR(info.st_mode)) {
                                printf(NOTICE_SUCCESS "Kaminowaku datastore exists.\n");
                        } else {
                                printf(NOTICE_ERROR "Kaminowaku datastore not as expected, aborting startup.\n");
                                FAILURE();
                                return 0;
                        }
                } else {
                        if (mkdir(_prog_data->wd, 0755) == NORMAL) {
                                printf(NOTICE_SUCCESS "Kaminowaku datastore successfully created.\n");
                                printf(NOTICE_INFO "Kaminowaku's datastore is at ~/.kaminowaku\n");
                        } else {
                                FAILURE();
                                return 0;
                        }
                }

                // Change program's working directory to kami root
                if (chdir(_prog_data->wd) != 0) {
                        FAILURE();
                        return 0;
                } else {
                        printf(NOTICE_SUCCESS "Working directory changed successfully.\n");
                }
                
                // Validate or Create KAMI_USER_LOGS_DIR
                if (stat(KAMI_USER_LOGS_DIR, &info) == NORMAL) {
                        if (S_ISDIR(info.st_mode)) {
                                printf(NOTICE_SUCCESS "\tLogging directory validated.\n");
                        } else {
                                printf(NOTICE_ERROR "\tLogging directory not as expected, aborting startup.\n");
                                FAILURE();
                                return 0;
                        }
                } else {
                        if (mkdir(KAMI_USER_LOGS_DIR, 0755) == NORMAL) {
                                printf(NOTICE_SUCCESS "\tLogging directory created.\n");
                        } else {
                                FAILURE();
                                return 0;
                        }
                }

                // Validate or Create KAMI_USER_PROFILES_DIR
                if (stat(KAMI_USER_PROFILES_DIR, &info) == NORMAL) {
                        if (S_ISDIR(info.st_mode)) {
                                printf(NOTICE_SUCCESS "\tProfiles directory validated.\n");
                        } else {
                                printf(NOTICE_ERROR "\tProfiles directory not as expected, aborting startup.\n");
                                FAILURE();
                                return 0;
                        }
                } else {
                        if (mkdir(KAMI_USER_PROFILES_DIR, 0755) == NORMAL) {
                                printf(NOTICE_SUCCESS "\tProfiles directory created.\n");
                        } else {
                                FAILURE();
                                return 0;
                        }
                }

                // Validate or Create KAMI_USER_PROJECTS_DIR
                if (stat(KAMI_USER_PROJECTS_DIR, &info) == NORMAL) {
                        if (S_ISDIR(info.st_mode)) {
                                printf(NOTICE_SUCCESS "\tProjects directory validated.\n");
                        } else {
                                printf(NOTICE_ERROR "\tProjects directory not as expected, aborting startup.\n");
                                FAILURE();
                                return 0;
                        }
                } else {
                        if (mkdir(KAMI_USER_PROJECTS_DIR, 0755) == NORMAL) {
                                printf(NOTICE_SUCCESS "\tProjects directory created.\n");
                        } else {
                                FAILURE();
                                return 0;
                        }
                }

                // Validate or Create KAMI_USER_TOOLS_DIR
                if (stat(KAMI_USER_TOOLS_DIR, &info) == NORMAL) {
                        if (S_ISDIR(info.st_mode)) {
                                printf(NOTICE_SUCCESS "\tTools directory validated.\n");
                        } else {
                                printf(NOTICE_ERROR "\tTools directory not as expected, aborting startup.\n");
                                FAILURE();
                                return 0;
                        }
                } else {
                        if (mkdir(KAMI_USER_TOOLS_DIR, 0755) == NORMAL) {
                                printf(NOTICE_SUCCESS "\tTools directory created.\n");
                        } else {
                                FAILURE();
                                return 0;
                        }
                }

                // Validate or Create KAMI_USER_BOOKS_DIR
                if (stat(KAMI_USER_BOOKS_DIR, &info) == NORMAL) {
                        if (S_ISDIR(info.st_mode)) {
                                printf(NOTICE_SUCCESS "\tBooks directory validated.\n");
                        } else {
                                printf(NOTICE_ERROR "\tBooks directory not as expected, aborting startup.\n");
                                FAILURE();
                                return 0;
                        }
                } else {
                        if (mkdir(KAMI_USER_BOOKS_DIR, 0755) == NORMAL) {
                                printf(NOTICE_SUCCESS "\tBooks directory created.\n");
                        } else {
                                FAILURE();
                                return 0;
                        }
                }

                // Validate or Create KAMI_USER_BOOK_MODULES_DIR
                if (stat(KAMI_USER_BOOK_MODULES_DIR, &info) == NORMAL) {
                        if (S_ISDIR(info.st_mode)) {
                                printf(NOTICE_SUCCESS "\tBook modules directory validated.\n");
                        } else {
                                printf(NOTICE_ERROR "\tBook modules directory not as expected, aborting startup.\n");
                                FAILURE();
                                return 0;
                        }
                } else {
                        if (mkdir(KAMI_USER_BOOK_MODULES_DIR, 0755) == NORMAL) {
                                printf(NOTICE_SUCCESS "\tBook modules directory created.\n");
                        } else {
                                FAILURE();
                                return 0;
                        }
                }

                // @@ Create runtime log
                char runtime_log_buffer[MAX_BLOCK];
                memset(runtime_log_buffer, 0x00, MAX_BLOCK);
                strcats(runtime_log_buffer, MAX_BLOCK, KAMI_USER_LOGS_DIR);
                strcats(runtime_log_buffer, MAX_BLOCK, file_timestamp_ns());
                strcats(runtime_log_buffer, MAX_BLOCK, "kaminowaku.log");
                FILE * kaminowaku_logfile = fopen(runtime_log_buffer, "a");
                if (kaminowaku_logfile == NULL) {
                        FAILURE();
                        return 0;
                } else {
                        _prog_data->log = (void*)kaminowaku_logfile;
                        printf(NOTICE_SUCCESS "Runtime logging active: %s%s\n", _prog_data->wd, runtime_log_buffer);
                        fprintf((FILE*)_prog_data->log, "--[%s]--\n" NOTICE_SUCCESS "Logging intialized.\n", timestamp()); fflush((FILE*)_prog_data->log);
                }

                // @@ Initialize Master Index
                _prog_data->active_project_flower = malloc(sizeof(FLOWER));
                if (!_prog_data->active_project_flower) {
                        fprintf((FILE*)_prog_data->log, "--[%s]--\n" NOTICE_ERROR "Failure to initialize master index.\n", timestamp()); fflush((FILE*)_prog_data->log);
                        FAILURE();
                        return 0;
                } else {
                        memset(_prog_data->active_project_flower->TID, 0x00, BLOCK);
                        _prog_data->active_project_flower->FD = FD_RESET;
                        _prog_data->active_project_flower->LAST_MAP = RESET;
                        _prog_data->active_project_flower->MAP_SIZE = RESET;
                        _prog_data->active_project_flower->MAP_ADDRESS = NULL;
                        _prog_data->active_project_flower->PETAL = NULL;
                        _prog_data->active_project_flower->NEXT = NULL;
                }

                // @@ Initialize Anchor Petal
                _prog_data->active_project_flower->PETAL = malloc(sizeof(TARGET));                
                if (!_prog_data->active_project_flower->PETAL) {
                        fprintf(
                                (FILE*)_prog_data->log,
                                "--[%s]--\n" NOTICE_ERROR "Failure to initialize target list's localhost anchor data.\n",
                                timestamp()
                        );
                        fflush((FILE*)_prog_data->log);
                        FAILURE();
                        return 0;
                } else {
                        // Initialize Anchor Data
                        memset(_prog_data->active_project_flower->PETAL->URL, 0x00, MAX_BLOCK);
                        memset(_prog_data->active_project_flower->PETAL->IPV4, 0x00, DOUBLE_BLOCK);
                        memset(_prog_data->active_project_flower->PETAL->IPV6, 0x00, SUPER_BLOCK);
                        memset(_prog_data->active_project_flower->PETAL->MAC, 0x00, SUPER_BLOCK);
                        memset(_prog_data->active_project_flower->PETAL->LAST_SCAN_TIME, 0x00, SUPER_BLOCK);
                        memset(_prog_data->active_project_flower->PETAL->NOTE, 0x00, SUPSUP_BLOCK);
                        for (int i = 0; i < MAX_PORTS; i++) {
                                _prog_data->active_project_flower->PETAL->PORTS[i] = PORT_NOT_SCANNED;
                        }
                        // Set Anchor Data
                        gethostname(_prog_data->active_project_flower->PETAL->URL, sizeof(_prog_data->active_project_flower->PETAL->URL)); // Could add a failure case here?
                        strcats(
                                _prog_data->active_project_flower->PETAL->IPV4,
                                sizeof(_prog_data->active_project_flower->PETAL->IPV4),
                                "127.0.0.1"
                        ); // If you don't have 127.0.0.1 as your loopback, you're insane.
                        // Log Success
                        fprintf((FILE*)_prog_data->log, "--[%s]--\n" NOTICE_SUCCESS "Anchor data initialized.\n", timestamp()); fflush((FILE*)_prog_data->log);
                        printf(NOTICE_SUCCESS "Anchor data integrated.\n");
                }

                //&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
                // !! <><><><><> <-- LOAD PROFILE HERE!!!!
                //&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
                if (load_global_profile((const unsigned char *)"default.ini", _prog_data) == NORMAL) {
                        fprintf((FILE*)_prog_data->log, "--[%s]--\n" NOTICE_SUCCESS "Global profile loaded.\n", timestamp()); fflush((FILE*)_prog_data->log);
                        printf(NOTICE_SUCCESS "Global profile loaded.\n");
                } else {
                        fprintf((FILE*)_prog_data->log, "--[%s]--\n" NOTICE_ERROR "Global profile failure.\n", timestamp()); fflush((FILE*)_prog_data->log);
                        printf(
                                NOTICE_ERROR "%s\n",
                                gprofile_last_error()
                        );
                        printf(NOTICE_ERROR "Failed to load global profile, aborting startup.\n");
                        fflush((FILE*)_prog_data->log);
                        FAILURE();
                        return 0;
                }

                /* >>> Reset error as no critical failure was reached. <<< */
                errno = 0;
                fprintf((FILE*)_prog_data->log, "--[%s]--\n" NOTICE_SUCCESS "Startup complete.", timestamp()); fflush((FILE*)_prog_data->log);

                // Set initial status banner and print the ascii art on first boot
                printf(NOTICE_SUCCESS "Initialization complete.\n"
                        RGB_COLOR_DARK_MAGENTA
                        "┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓\n┃ "
                        RGB_COLOR_BRIGHT_YELLOW ">>> "
                        ANSI_COLOR_RESET "BY USING THIS PROGRAM YOU AGREE TO ADHERE TO THE AUP"
                        RGB_COLOR_BRIGHT_YELLOW " <<<"
                        RGB_COLOR_DARK_MAGENTA
                        " ┃\n┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛"
                        ANSI_COLOR_RESET "\n"

                );
                printf("Press enter to continue...", _prog_data->prompt);
                fflush(stdout);
                if (fgets(_prog_data->cmd_input, INPUT_BLOCK - 1, stdin) == 0) {return -1;}
                if (_prog_data->cmd_input[INPUT_BLOCK - 3] != 0x00) {while ((getchar()) != 0x0A);}
                memset(_prog_data->cmd_input, 0x00, INPUT_BLOCK);
                //strcats(_prog_data->status_banner, sizeof(_prog_data->status_banner), RGB_COLOR_BRIGHT_YELLOW "No project selected...\n" ANSI_COLOR_RESET);
                _prog_data->f_type = S_DEFAULT;
                kui_enter(_prog_data);
                kui_render_page();
        }

        // @@ CONTEXT CATCHING IF STATEMENT, Know that it exists and move on.
        // When a non-standard f_type is defined (i.e. ftype > 1) call the corresponding output of cmd_scan
        /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        if (_prog_data->f_type > S_DEFAULT) {
                cmd_scan_safe(_prog_data);
        }

        // Gather user input
        /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        if (_prog_data->f_type == S_DEFAULT || _prog_data->f_type < S_DEFAULT) {
                kui_render_page();

                // @@ Print debug lines if flag is set
                if (_prog_data->debug_flag == ISTRUE) {
                        memset(_prog_data->prompt, 0x00, DOUBLE_BLOCK);
                        strcats((char*)_prog_data->prompt, sizeof(_prog_data->prompt), "[" RGB_COLOR_BRIGHT_YELLOW "DEBUG" ANSI_COLOR_RESET "]> ");
                }

                // Terminal Shell Type Handling
                memset(_prog_data->cmd_input, 0x00, INPUT_BLOCK);
                if (isatty(STDIN_FILENO)) {
                        fflush(stdout);
                        kui_flush_log();
                        enable_raw_mode();
                        read_line(_prog_data);
                        disable_raw_mode();
                        memset(_prog_data->cmd_last, 0x00, INPUT_BLOCK);
                        strcats(_prog_data->cmd_last, sizeof(_prog_data->cmd_last), _prog_data->cmd_input);
                        if(_prog_data->render_mode == RENDER_NORMAL) {
                                kui_clear_output();
                        }
                } else {
                        printf("%s", _prog_data->prompt);
                        fflush(stdout);
                        if (fgets(_prog_data->cmd_input, INPUT_BLOCK - 1, stdin) == 0) {return -1;}
                        if (_prog_data->cmd_input[INPUT_BLOCK - 3] != 0x00) {while ((getchar()) != 0x0A);}
                }
                kui_frame_start();

                // !! INPUT ERROR DETECTION ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
                //if (_prog_data->debug_flag == ISTRUE) {
                //        int b = 0;
                //        printf("
                //                ![" ANSI_COLOR_MAGENTA "kaminowaku()" ANSI_COLOR_RESET "]> " 
                //                ANSI_COLOR_CYAN "_prog_data->cmd_input" ANSI_COLOR_RESET " PRE-SANITIZATION: %s\n", _prog_data->cmd_input
                //        );
                //        for (b = 0; b < INPUT_BLOCK; b++) {
                //              if (b % 32 == 0 && b != 0) {
                //                      printf("\n");
                //              }
                //              printf("%02X ", _prog_data->cmd_input[b]);
                //        } printf(
                //              "\n![kaminowaku()]> Length PRE-SANITIZATION: %lu\n",sizeof(_prog_data->cmd_input) / sizeof(_prog_data->cmd_input[0])
                //        );
                //}
                // !! INPUT ERROR DETECTION ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
                SANITIZE_INPUT(_prog_data);
                CANONICALIZE_CMD_ALIAS(_prog_data);
                // !! INPUT ERROR DETECTION ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
                //if (_prog_data->debug_flag == ISTRUE) {
                //        int b = 0;
                //        printf(
                //                "![" ANSI_COLOR_MAGENTA "kaminowaku()" ANSI_COLOR_RESET "]> " 
                //                ANSI_COLOR_CYAN "_prog_data->cmd_input" ANSI_COLOR_RESET " POST-SANITIZATION: %s\n", _prog_data->cmd_input
                //        );
                //        for (b = 0; b < INPUT_BLOCK; b++) {
                //                if (b % 32 == 0 && b != 0) {
                //                        printf("\n");
                //                }
                //                printf("%02X ", _prog_data->cmd_input[b]);
                //        } printf(
                //                "\n![kaminowaku()]> Length POST-SANITIZATION: %lu\n",sizeof(_prog_data->cmd_input) / sizeof(_prog_data->cmd_input[0])
                //        );
                //}
                // !! INPUT ERROR DETECTION ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
                ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
                // @@ Tokenization
                if (_prog_data->cmd_tokens != NULL) {
                        input_tokens_free(_prog_data);
                        //if (_prog_data->debug_flag == ISTRUE) {
                        //        if (_prog_data->cmd_tokens == NULL) {
                        //                printf(
                        //                        "!["
                        //                        ANSI_COLOR_MAGENTA
                        //                        "kaminowaku()"
                        //                        ANSI_COLOR_RESET
                        //                        "]> Successfull free for " ANSI_COLOR_CYAN "_prog_data->cmd_tokens" ANSI_COLOR_RESET "\n"
                        //                );
                        //        }
                        //}   
                } input_tokenization(_prog_data);
                ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
                // !! INPUT ERROR DETECTION ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
                //if (_prog_data->debug_flag == ISTRUE) {
                //        printf(
                //                "![" ANSI_COLOR_MAGENTA "kaminowaku()" ANSI_COLOR_RESET "]> " 
                //                ANSI_COLOR_CYAN "_prog_data->cmd_tokens_count" ANSI_COLOR_RESET " = %d\n", _prog_data->cmd_tokens_count
                //        );
                //        for (int i = 0; i < _prog_data->cmd_tokens_count; i++) {
                //                printf(
                //                        "!["
                //                        ANSI_COLOR_MAGENTA
                        //                        "kaminowaku()"
                //                        ANSI_COLOR_RESET
                //                        "]> " ANSI_COLOR_CYAN "_prog_data->cmd_tokens" ANSI_COLOR_RESET
                //                        "[" ANSI_COLOR_CYAN "%d" ANSI_COLOR_RESET "] = '%s' " ANSI_COLOR_YELLOW "%p" ANSI_COLOR_RESET "\n",
                //                         i, _prog_data->cmd_tokens[i], (void*)_prog_data->cmd_tokens[i]
                //                );
                //        }
                //}
                // !! INPUT ERROR DETECTION ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

                // @@ Execute State-Device and catch exit
                if (strcmp(_prog_data->cmd_input, "exit") == MATCH) {
                        
                        // Unload first
                        _prog_data->f_type = C_UNLOAD;
                        cmd_scan(_prog_data);
                        
                        // Clean tokens and notify exit
                        input_tokens_free(_prog_data);
                        fprintf((FILE*)_prog_data->log, "--[%s]--\n" NOTICE_SUCCESS "Exit.\n", timestamp()); fflush((FILE*)_prog_data->log);
                        fclose((FILE*)_prog_data->log);
                        
                        // @@ EXIT TUI
                        kui_exit();
                        
                        if (_prog_data->debug_flag == ISTRUE) {
                                printf(NOTICE_SUCCESS "Exit.\n");        
                        } else {
                                printf("\033\143" NOTICE_SUCCESS "Exit.\n");
                        }
                        return 0;
                } 

                // @@ State-Device execution
                fprintf((FILE*)_prog_data->log, "--[%s]--\n> %s\n", timestamp(), _prog_data->cmd_input); fflush((FILE*)_prog_data->log);
                cmd_scan_safe(_prog_data);
                return 1337;
        }
        return 1337; // Don't ask questions.
}
