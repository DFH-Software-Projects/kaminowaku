// Copyright 2026 Jamison A. Drapeau
#include "targets.h"
#include "tlib.h"
#include "helpers.h"
#include "kui.h"
#include "kportdisplay.h"
#include "ktargetdisplay_args.h"
#include "kscan.h"
#include "book_persist.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <time.h>
#include <stdio.h>
#include <regex.h>
#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/mman.h>

// Define preservation param for readability
#define PRESERVE 1
#define NO_PRESERVE 0

// FBV TUPLE
typedef struct {
        unsigned char   TID[TID_BLOCK];        // NUL-terminated TID
        char            PATH[PATH_MAX];        // Full petal path
} FBV_TUPLE;

// Static internal instances with predictable states.
static FBV_TUPLE FBVT_ZERO = { .TID = {0}, .PATH = {0} };
static FBV_TUPLE FBVT      = { .TID = {0}, .PATH = {0} };

// Reset:
static inline void fbvt_clear(void) {
        memcpy(&FBVT, &FBVT_ZERO, sizeof(FBVT));
}

// Set:
static inline void fbvt_set(const unsigned char *TID, const char *PATH) {
        memset(FBVT.TID, 0x00, TID_BLOCK);
        if (TID) {
                size_t n = strnlen((const char *)TID, TID_BLOCK);
                if (n >= TID_BLOCK) n = TID_BLOCK - 1;
                memcpy(FBVT.TID, TID, n);
        }
        if (PATH) {
                size_t m = strnlen(PATH, sizeof(FBVT.PATH));
                if (m >= sizeof(FBVT.PATH)) m = sizeof(FBVT.PATH) - 1;
                memset(FBVT.PATH, 0x00, sizeof(FBVT.PATH));
                memcpy(FBVT.PATH, PATH, m);
        } else {
                FBVT.PATH[0] = '\0';
        }
}

// Drop in returns
inline const unsigned char *fbvt_tid(void)  { return FBVT.TID;  }
inline const char *fbvt_path(void)          { return FBVT.PATH; }

// @@ Find by value, return TID
// Many thanks to the robot with helping here, lots of math involved
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
const unsigned char * targets_find_by_value_return_tid(_carry_forward * _prog_data, char IDENTIFIER, const unsigned char *VALUE, int KEEP_PATH) {
        if (!_prog_data || !_prog_data->active_project_flower || !VALUE) {return NULL;}         // Check invalids
        fbvt_clear();                                                                           // Clear Tuple
        FLOWER *CURRENT = _prog_data->active_project_flower->NEXT;                              // Load and skip anchor
        while (CURRENT) {                                                                       // Cycle

                // 1.) Build pathing for FLOWER node
                char PATH[PATH_MAX];
                if (build_petal_path(PATH, sizeof(PATH), _prog_data->wd, _prog_data->active_project, CURRENT->TID) != NORMAL) {
                        CURRENT = CURRENT->NEXT;
                        continue;
                }

                // 2.) Try to open
                int flags = O_RDONLY;
                int FD = open(PATH, flags);
                if (FD < 0) {
                        CURRENT = CURRENT->NEXT;
                        continue;
                }

                // 3.) Direct read TARGET (no mmaps for now)
                TARGET PETAL;
                ssize_t R = pread(FD, &PETAL, sizeof(PETAL), 0);
                if (R != (ssize_t)sizeof(PETAL)) {
                        close(FD);
                        CURRENT = CURRENT->NEXT;
                        continue;
                }

                // !! DEBUG LINES
                if (_prog_data->debug_flag == ISTRUE) {
                        kui_add_line_and_render(
                                "!["
                                ANSI_COLOR_MAGENTA
                                "targets_find_by_value_return_tid()"
                                ANSI_COLOR_RESET
                                "]> GATHERING PETAL DATA:"
                        );
                        // ---
                        kui_add_line_and_render(
                                "!["
                                ANSI_COLOR_MAGENTA
                                "targets_find_by_value_return_tid()"
                                ANSI_COLOR_RESET
                                "]> "
                                ANSI_COLOR_CYAN
                                "URL "
                                ANSI_COLOR_RESET
                                " = "
                                ANSI_COLOR_CYAN
                                "%s"
                                ANSI_COLOR_RESET
                                , PETAL.URL
                        );
                        kui_add_line_and_render(
                                "!["
                                ANSI_COLOR_MAGENTA
                                "targets_find_by_value_return_tid()"
                                ANSI_COLOR_RESET
                                "]> "
                                ANSI_COLOR_CYAN
                                "IPv4"
                                ANSI_COLOR_RESET
                                " = "
                                ANSI_COLOR_CYAN
                                "%s"
                                ANSI_COLOR_RESET
                                , PETAL.IPV4
                        );
                        kui_add_line_and_render(
                                "!["
                                ANSI_COLOR_MAGENTA
                                "targets_find_by_value_return_tid()"
                                ANSI_COLOR_RESET
                                "]> "
                                ANSI_COLOR_CYAN
                                "IPv6"
                                ANSI_COLOR_RESET
                                " = "
                                ANSI_COLOR_CYAN
                                "%s"
                                ANSI_COLOR_RESET
                                , PETAL.IPV6
                        );
                        kui_add_line_and_render(
                                "!["
                                ANSI_COLOR_MAGENTA
                                "targets_find_by_value_return_tid()"
                                ANSI_COLOR_RESET
                                "]> "
                                ANSI_COLOR_CYAN
                                "MAC "
                                ANSI_COLOR_RESET
                                " = "
                                ANSI_COLOR_CYAN
                                "%s"
                                ANSI_COLOR_RESET
                                , PETAL.MAC
                        );
                }

                // 4.) Compare directly from PETAL (no second pread, no stack buffer)
                int8_t match = ISFALSE; // Innocent until guilty
                size_t vlen = strnlen((const char*)VALUE, MAX_BLOCK);         // cap = expected max len
                switch (IDENTIFIER) {
                        case 0x55: { // U
                                size_t flen = strnlen((const char *)PETAL.URL, URL_BLOCK);
                                if (vlen == flen && memcmp((char*)VALUE, (char*)PETAL.URL, vlen) == NORMAL) {match = ISTRUE;}
                                break;
                        }
                        case 0x34: { // 4
                                size_t flen = strnlen((const char *)PETAL.IPV4, IPV4_BLOCK);
                                if (vlen == flen && memcmp((char*)VALUE, (char*)PETAL.IPV4, vlen) == NORMAL) {match = ISTRUE;}
                                break;
                        }
                        case 0x36: { // 6
                                size_t flen = strnlen((const char *)PETAL.IPV6, IPV6_BLOCK);
                                if (vlen == flen && memcmp((char*)VALUE, (char*)PETAL.IPV6, vlen) == NORMAL) {match = ISTRUE;}
                                break;
                        }
                        case 0x4D: { // M
                                size_t flen = strnlen((const char *)PETAL.MAC, MAC_BLOCK);
                                if (vlen == flen && memcmp((char*)VALUE, (char*)PETAL.MAC, vlen) == NORMAL) {match = ISTRUE;}
                                break;
                        }
                }
                if (match) {                                                            // Handle Conditional for true/false matching
                        unsigned char TIDBUF[TID_BLOCK];                                // Stack alloc TID Buffer
                        memset(TIDBUF, 0x00, TID_BLOCK);                                // Init TID Buffer
                        memcpy(TIDBUF, CURRENT->TID, TID_BLOCK);                        // Copy out field data
                        TIDBUF[TID_BLOCK - 1] = 0x00;                                   // Forcefully null terminate (harmless with prior memset)
                        if (KEEP_PATH) {
                                fbvt_set(TIDBUF, PATH);                                  // PRESERVE PATH
                        } else {
                                fbvt_set(TIDBUF, NULL);                                  // NO_PRESERVE
                        }
                        close(FD);
                        return fbvt_tid();                                              // Return match
                }
                // No match for this PETAL
                close(FD);
                CURRENT = CURRENT->NEXT;
        }
        // No matching target in FLOWER
        return NULL;
}
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

// @@ Parse and validate an IPv4 or IPv6 CIDR network
static int targets_cidr_parse(
        const char *CIDR,
        int FAMILY,
        unsigned char *ADDRESS,
        int *PREFIX
) {
        char BUFFER[IPV6_BLOCK + 8];
        char *SLASH;
        char *END;
        long PREFIX_VALUE;
        int MAX_PREFIX;

        if (!CIDR || !ADDRESS || !PREFIX) return ABNORMAL;
        if (strnlen(CIDR, sizeof(BUFFER)) >= sizeof(BUFFER)) return ABNORMAL;

        memset(BUFFER, 0x00, sizeof(BUFFER));
        snprintf(BUFFER, sizeof(BUFFER), "%s", CIDR);

        SLASH = strrchr(BUFFER, '/');
        if (!SLASH) return ABNORMAL;

        *SLASH = 0x00;
        SLASH++;
        if (BUFFER[0] == 0x00 || SLASH[0] == 0x00) return ABNORMAL;

        PREFIX_VALUE = strtol(SLASH, &END, 10);
        if (*END != 0x00) return ABNORMAL;

        MAX_PREFIX = (FAMILY == AF_INET) ? 32 : 128;
        if (PREFIX_VALUE < 0 || PREFIX_VALUE > MAX_PREFIX) return ABNORMAL;

        memset(ADDRESS, 0x00, 16);
        if (inet_pton(FAMILY, BUFFER, ADDRESS) != 1) return ABNORMAL;

        *PREFIX = (int)PREFIX_VALUE;
        return NORMAL;
}

// @@ Normalize an address to the first address in its CIDR block
static void targets_cidr_normalize(unsigned char *ADDRESS, int ADDRESS_BYTES, int PREFIX) {
        int FULL_BYTES = PREFIX / 8;
        int REMAINING_BITS = PREFIX % 8;

        if (REMAINING_BITS != 0) {
                ADDRESS[FULL_BYTES] &= (unsigned char)(0xFFu << (8 - REMAINING_BITS));
                FULL_BYTES++;
        }

        for (int i = FULL_BYTES; i < ADDRESS_BYTES; i++) {
                ADDRESS[i] = 0x00;
        }
}

// @@ Increment a binary IPv4 or IPv6 address by one
static void targets_cidr_increment(unsigned char *ADDRESS, int ADDRESS_BYTES) {
        for (int i = ADDRESS_BYTES - 1; i >= 0; i--) {
                ADDRESS[i]++;
                if (ADDRESS[i] != 0x00) break;
        }
}

// @@ Calculate CIDR population while enforcing the configured batch limit
static int targets_cidr_population(
        int ADDRESS_BITS,
        int PREFIX,
        unsigned int LIMIT,
        uint64_t *COUNT
) {
        int HOST_BITS;
        uint64_t CALCULATED;

        if (!COUNT || PREFIX < 0 || PREFIX > ADDRESS_BITS) return ABNORMAL;

        HOST_BITS = ADDRESS_BITS - PREFIX;

        /* limit_max_targets_per_batch is unsigned int. Any block with 32 or
         * more host bits necessarily exceeds every representable nonzero limit. */
        if (HOST_BITS >= 32) return ABNORMAL;

        CALCULATED = ((uint64_t)1 << HOST_BITS);
        if (CALCULATED > (uint64_t)LIMIT) return ABNORMAL;

        *COUNT = CALCULATED;
        return NORMAL;
}

// @@ Expand a CIDR network through the canonical single-target add path
static int targets_add_cidr(_carry_forward * _prog_data) {
        int FAMILY;
        int ADDRESS_BYTES;
        int ADDRESS_BITS;
        int PREFIX;
        uint64_t TARGET_COUNT;
        unsigned char ADDRESS[16];
        char ADDRESS_STRING[IPV6_BLOCK];
        unsigned char *SAVED_TOKEN_2;
        unsigned char *SAVED_TOKEN_3;
        c_size_t SAVED_TOKEN_COUNT;

        if (!_prog_data || _prog_data->cmd_tokens_count != 5) return ABNORMAL;

        if (strcmp((char*)_prog_data->cmd_tokens[3], "-4") == MATCH) {
                FAMILY = AF_INET;
                ADDRESS_BYTES = 4;
                ADDRESS_BITS = 32;
        } else if (strcmp((char*)_prog_data->cmd_tokens[3], "-6") == MATCH) {
                FAMILY = AF_INET6;
                ADDRESS_BYTES = 16;
                ADDRESS_BITS = 128;
        } else {
                targets_print_usage();
                return ABNORMAL;
        }

        if (
                targets_cidr_parse(
                        (char*)_prog_data->cmd_tokens[4],
                        FAMILY,
                        ADDRESS,
                        &PREFIX
                ) != NORMAL
        ) {
                kui_add_line(
                        NOTICE_WARNING
                        RGB_COLOR_BRIGHT_YELLOW "%s" ANSI_COLOR_RESET
                        " is not a valid %s CIDR network."
                        , _prog_data->cmd_tokens[4]
                        , (FAMILY == AF_INET) ? "IPv4" : "IPv6"
                );
                return ABNORMAL;
        }

        if (
                targets_cidr_population(
                        ADDRESS_BITS,
                        PREFIX,
                        _prog_data->gprof.limit_max_targets_per_batch,
                        &TARGET_COUNT
                ) != NORMAL
        ) {
                kui_add_line(
                        NOTICE_WARNING
                        "CIDR network exceeds "
                        ANSI_COLOR_CYAN "limits.max_targets_per_batch"
                        ANSI_COLOR_RESET " (%u)."
                        , _prog_data->gprof.limit_max_targets_per_batch
                );
                return ABNORMAL;
        }

        targets_cidr_normalize(ADDRESS, ADDRESS_BYTES, PREFIX);
        memset(ADDRESS_STRING, 0x00, sizeof(ADDRESS_STRING));

        if (!inet_ntop(FAMILY, ADDRESS, ADDRESS_STRING, sizeof(ADDRESS_STRING))) {
                kui_add_line(NOTICE_ERROR "Failure to normalize CIDR network.");
                return ABNORMAL;
        }

        kui_add_line(
                NOTICE_INFO "Expanding "
                ANSI_COLOR_CYAN "%s/%d" ANSI_COLOR_RESET
                " into " ANSI_COLOR_CYAN "%llu" ANSI_COLOR_RESET " targets."
                , ADDRESS_STRING
                , PREFIX
                , (unsigned long long)TARGET_COUNT
        );

        SAVED_TOKEN_2 = _prog_data->cmd_tokens[2];
        SAVED_TOKEN_3 = _prog_data->cmd_tokens[3];
        SAVED_TOKEN_COUNT = _prog_data->cmd_tokens_count;

        _prog_data->cmd_tokens[2] = SAVED_TOKEN_3;
        _prog_data->cmd_tokens_count = 4;

        for (uint64_t i = 0; i < TARGET_COUNT; i++) {
                memset(ADDRESS_STRING, 0x00, sizeof(ADDRESS_STRING));
                if (!inet_ntop(FAMILY, ADDRESS, ADDRESS_STRING, sizeof(ADDRESS_STRING))) {
                        kui_add_line(NOTICE_ERROR "Failure converting network address.");
                        _prog_data->cmd_tokens[2] = SAVED_TOKEN_2;
                        _prog_data->cmd_tokens[3] = SAVED_TOKEN_3;
                        _prog_data->cmd_tokens_count = SAVED_TOKEN_COUNT;
                        return ABNORMAL;
                }

                _prog_data->cmd_tokens[3] = (unsigned char*)ADDRESS_STRING;
                targets_add_target(_prog_data);
                targets_cidr_increment(ADDRESS, ADDRESS_BYTES);
        }

        _prog_data->cmd_tokens[2] = SAVED_TOKEN_2;
        _prog_data->cmd_tokens[3] = SAVED_TOKEN_3;
        _prog_data->cmd_tokens_count = SAVED_TOKEN_COUNT;
        return NORMAL;
}


//======================================================================================================================
//======================================================================================================================
//======================================================================================================================
//===================================================================================================[Primary Functions]

// @@ Print usage statement
void targets_print_usage(void) {
        kui_add_line("< Usage:");
        kui_add_line("");
        // Targets add|del usage
        kui_add_line(                
                "\ttargets [add | del ["
                ANSI_COLOR_CYAN         "<TID>"
                ANSI_COLOR_RESET        "]] [-U "
                ANSI_COLOR_CYAN         "<URL>"
                ANSI_COLOR_RESET        "][-4 "
                ANSI_COLOR_CYAN         "<IPv4 Address>"
                ANSI_COLOR_RESET        "][-6 "
                ANSI_COLOR_CYAN         "<IPv6 Address>"
                ANSI_COLOR_RESET        "][-M "
                ANSI_COLOR_CYAN         "<MAC Address>"
                ANSI_COLOR_RESET        "]]"
        );
        kui_add_line(
                "\t" NOTICE_INFO
                ANSI_COLOR_YELLOW "Add or Delete a target." ANSI_COLOR_RESET
        );
        kui_add_line("");
        kui_add_line("\ttargets del -n");
        kui_add_line(
                "\t" NOTICE_INFO
                ANSI_COLOR_YELLOW "Delete targets with no received-packet observation." ANSI_COLOR_RESET
        );
        kui_add_line("");
        // CIDR add usage:
        kui_add_line(
                "\ttargets add -net [-4 | -6] "
                ANSI_COLOR_CYAN         "<CIDR Network>"
                ANSI_COLOR_RESET
        );
        kui_add_line(
                "\t" NOTICE_INFO
                ANSI_COLOR_YELLOW "Expand an IPv4 or IPv6 CIDR network into individual targets." ANSI_COLOR_RESET
        );
        kui_add_line("");
        // Target Context Usage:
        kui_add_line(
                "\ttargets [["
                ANSI_COLOR_CYAN         "<TID>" 
                ANSI_COLOR_RESET        "] | [-U46M] "
                ANSI_COLOR_CYAN         "<Identifier>" 
                ANSI_COLOR_RESET        "]"
        );
        kui_add_line(
                "\t[set [-U46MN] "
                ANSI_COLOR_CYAN         "<Value>" 
                ANSI_COLOR_RESET        " | display | dt]"
        );
        kui_add_line(
                "\t" NOTICE_INFO 
                ANSI_COLOR_YELLOW "Enter into target context for display, modification, or scanning."  ANSI_COLOR_RESET
        );
        kui_add_line("");
        // Targets display usage:
        kui_add_line(
                "\ttargets display [ -d | -o | -p "
                ANSI_COLOR_CYAN         "<Port>"
                ANSI_COLOR_RESET        " | -b "
                ANSI_COLOR_CYAN         "<Port>"
                ANSI_COLOR_RESET        " ]"
        );
        kui_add_line(
                "\t" NOTICE_INFO 
                ANSI_COLOR_YELLOW "Display all targets in a project either in a compact format, or with enumeration data." ANSI_COLOR_RESET
        );
        kui_add_line("");
        // Arg translations
        kui_add_line("\t-net\t: Expand a CIDR network into individual targets (add only)");
        kui_add_line("\t-U\t: URL");
        kui_add_line("\t-4\t: IPv4 Address");
        kui_add_line("\t-6\t: IPv6 Address");
        kui_add_line("\t-M\t: MAC Address");
        kui_add_line("\t-N\t: Note (e.g. 'DC' or 'Webserver' or 'Bobs Computer')");
        kui_add_line("\t-n\t: Delete targets with no received-packet observation (del only)");
        kui_add_line("\t-d\t: Display all detailed built-in scan data followed by all stored Book output");
        kui_add_line("\t-o\t: Display only targets and scan results backed by received packets");
        kui_add_line("\t-p\t: Display only targets with the specified TCP port OPEN");
        kui_add_line("\t-b\t: Display detailed built-in banner/service data for the specified OPEN TCP port");
        kui_add_line("");
        return;
}

// @@ Add a target
void targets_add_target(_carry_forward * _prog_data) {
        if (
                _prog_data
                &&
                _prog_data->cmd_tokens_count == 5
                &&
                strcmp((char*)_prog_data->cmd_tokens[2], "-net") == MATCH
        ) {
                (void)targets_add_cidr(_prog_data);
                return;
        }

        int8_t TARGET_DIRECTORY_EXISTS = ISFALSE;
        int8_t ALL_DATA_VALID = ISTRUE;
        int8_t ALL_PARAMS_VALID = ISTRUE;
        int8_t URL = ISFALSE;              // These are for identifier tracking which is fine
        int8_t IP4 = ISFALSE;
        int8_t IP6 = ISFALSE;
        int8_t MAC = ISFALSE;
        static const char ROTUNDA[] = 
                "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                "abcdefghijklmnopqrstuvwxyz"
                "0123456789"; // + '\0'
        static const c_size_t ROTUNDA_SIZE = sizeof(ROTUNDA) - 1; // Excluding null term
        struct stat info;
        char TMP_TID[TID_BLOCK];
        do {
                memset(TMP_TID, 0x00, TID_BLOCK);
                for (c_size_t i = 0; i < TID_BLOCK; i++) {
                        TMP_TID[i] = ROTUNDA[arc4random_uniform(ROTUNDA_SIZE)];
                } TMP_TID[BLOCK] = 0x00;
                char td[MAX_BLOCK]; targets_buffer_path_stack_unsafe(_prog_data, MAX_BLOCK, TMP_TID, td);
                if (_prog_data->debug_flag == ISTRUE) {
                        kui_add_line_and_render("![targets_add_target()]> TD: %s", td);
                }
                if (stat(td, &info) != MATCH) {
                        TARGET * TMP_TARGET = targets_buffer_target();
                        for (int i = 0; i < MAX_PORTS; i++) {
                                TMP_TARGET->PORTS[i] = PORT_NOT_SCANNED;
                        }
                        for (unsigned long i = 2; i < _prog_data->cmd_tokens_count; i++) {
                                if (
                                        (i % 2 == 0)
                                        &&
                                        (_prog_data->cmd_tokens_count > (i + 1))
                                ) {
                                        switch(_prog_data->cmd_tokens[i][1]) {
                                                case 0x55: {
                                                        if (!URL) {
                                                                if(targets_modify_petal_url(_prog_data, TMP_TARGET, _prog_data->cmd_tokens[i+1]) != ABNORMAL) {
                                                                        URL = ISTRUE;
                                                                } else {
                                                                        ALL_DATA_VALID = ISFALSE;
                                                                }
                                                        }
                                                        break;
                                                }
                                                case 0x34: {
                                                        if (!IP4) {
                                                                if(targets_modify_petal_ipv4(_prog_data, TMP_TARGET, _prog_data->cmd_tokens[i+1]) != ABNORMAL) {
                                                                        IP4 = ISTRUE;
                                                                } else {
                                                                        ALL_DATA_VALID = ISFALSE;
                                                                }
                                                        }
                                                        break;
                                                }
                                                case 0x36: {
                                                        if (!IP6) {
                                                                if(targets_modify_petal_ipv6(_prog_data, TMP_TARGET, _prog_data->cmd_tokens[i+1]) != ABNORMAL) {
                                                                        IP6 = ISTRUE;
                                                                } else {
                                                                        ALL_DATA_VALID = ISFALSE;
                                                                }
                                                        }
                                                        break;
                                                }
                                                case 0x4D: {
                                                        if (!MAC) {
                                                                if(targets_modify_petal_mac(_prog_data, TMP_TARGET, _prog_data->cmd_tokens[i+1]) != ABNORMAL) {
                                                                        MAC = ISTRUE;
                                                                } else {
                                                                        ALL_DATA_VALID = ISFALSE;
                                                                }
                                                        }
                                                        break;
                                                }
                                                default: {
                                                        ALL_PARAMS_VALID = ISFALSE;
                                                        break;
                                                }
                                        }
                                }
                        }
                        if (ALL_DATA_VALID == ISTRUE && ALL_PARAMS_VALID == ISTRUE) {
                                FLOWER * TMP_FLOWER_NODE = flower_create_node(_prog_data->active_project_flower, TMP_TID);
                                if (TMP_FLOWER_NODE) {
                                        if (mkdir((char*)td, 0755) == NORMAL) {
                                                _prog_data->active_project_has_targets = ISTRUE;
                                                kui_add_line(
                                                        NOTICE_SUCCESS "Target with ID"
                                                        ANSI_COLOR_CYAN " %s " ANSI_COLOR_RESET
                                                        "successfully added."
                                                        , (char*)TMP_TID
                                                );
                                                kui_add_line(
                                                        NOTICE_INFO "Target data can be found at: "
                                                        ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET
                                                        , td
                                                );
                                                ++_prog_data->active_project_target_count;
                                                TARGET_DIRECTORY_EXISTS = ISTRUE;
                                                if (_prog_data->debug_flag == ISTRUE) {
                                                        kui_add_line_and_render(
                                                                "!["
                                                                ANSI_COLOR_MAGENTA
                                                                "targets_add_target()"
                                                                ANSI_COLOR_RESET
                                                                "]> New target: " ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET
                                                                , (char*)TMP_TID
                                                        );
                                                        kui_add_line_and_render(
                                                                "!["
                                                                ANSI_COLOR_MAGENTA
                                                                "targets_add_target()"
                                                                ANSI_COLOR_RESET
                                                                "]> " ANSI_COLOR_CYAN "ANCHOR" ANSI_COLOR_RESET
                                                                " @ " ANSI_COLOR_YELLOW "%p" ANSI_COLOR_RESET
                                                                , (void*)_prog_data->active_project_flower
                                                        );
                                                        kui_add_line_and_render(
                                                                "!["
                                                                ANSI_COLOR_MAGENTA
                                                                "targets_add_target()"
                                                                ANSI_COLOR_RESET
                                                                "]> " ANSI_COLOR_CYAN "NEW" ANSI_COLOR_RESET
                                                                " @ " ANSI_COLOR_YELLOW "%p" ANSI_COLOR_RESET
                                                                , (void*)TMP_FLOWER_NODE
                                                        );
                                                        kui_add_line_and_render(
                                                                "!["
                                                                ANSI_COLOR_MAGENTA
                                                                "targets_add_target()"
                                                                ANSI_COLOR_RESET
                                                                "]> " ANSI_COLOR_CYAN "NEXT" ANSI_COLOR_RESET
                                                                " @ " ANSI_COLOR_YELLOW "%p" ANSI_COLOR_RESET
                                                                , (void*)TMP_FLOWER_NODE->NEXT
                                                        );
                                                }
                                                strcats(td, sizeof(td), "/.data");
                                                if (targets_write_petal_data(td, TMP_TARGET) != NORMAL) {
                                                        kui_add_line( 
                                                                NOTICE_ERROR "Failure to write target data."
                                                        );
                                                }
                                        } else {
                                                kui_add_line(
                                                        NOTICE_ERROR "Failure to create directory."
                                                );
                                        }
                                } else if (_prog_data->debug_flag == ISTRUE) {
                                        kui_add_line_and_render(
                                                "![" ANSI_COLOR_MAGENTA "targets_add_target()" ANSI_COLOR_RESET "]> Failure to allocate data."
                                        );
                                }
                        } else {
                                kui_add_line(
                                        NOTICE_NOPE "Will not add targets with invalid parameters."
                                );
                                free(TMP_TARGET);
                                targets_print_usage();
                                break;
                        }
                        free(TMP_TARGET);
                } 
        } while (TARGET_DIRECTORY_EXISTS = ISFALSE);
        return;
}

// @@ Delete a target from the list
void targets_del_target(_carry_forward * _prog_data, int8_t MODE) {
        if (_prog_data->debug_flag == ISTRUE) {
                kui_add_line_and_render(
                       "!["
                       ANSI_COLOR_MAGENTA
                       "targets_del_target()"
                       ANSI_COLOR_RESET
                       "]> "
                       ANSI_COLOR_CYAN
                       "MODE "
                       ANSI_COLOR_RESET
                       "= "
                       ANSI_COLOR_CYAN
                       "%d"
                       ANSI_COLOR_RESET
                       , MODE
                );
        }
        const unsigned char * TMP_TID = NULL;
        if (MODE == MODE_IDENTIFIER) {
                TMP_TID = targets_find_by_value_return_tid(_prog_data, _prog_data->cmd_tokens[2][1], _prog_data->cmd_tokens[3], NO_PRESERVE);
                if (TMP_TID) {
                        MODE = MODE_TID;
                }
        } else {
                if (vtid(_prog_data->cmd_tokens[2]) == NORMAL) {
                        TMP_TID = _prog_data->cmd_tokens[2];
                } else {
                        kui_add_line(
                                NOTICE_WARNING
                                RGB_COLOR_BRIGHT_YELLOW "%s" ANSI_COLOR_RESET " is invalid."
                                , _prog_data->cmd_tokens[2]
                        );
                        kui_add_line(
                                NOTICE_INFO "Without -[U|4|6|M] specified, a valid "
                                ANSI_COLOR_CYAN
                                "TID"
                                ANSI_COLOR_RESET
                                " must be provided."
                        );
                        return;
                }
        }
        if (MODE == MODE_TID) {
                if (targets_delete_by_tid(_prog_data, TMP_TID) == NORMAL) {
                        kui_add_line(
                                 NOTICE_SUCCESS "Deleted "
                                 ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET " from "
                                 ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET ".",
                                 TMP_TID, _prog_data->active_project
                        );
                        return;
                }
        }
        kui_add_line(
                NOTICE_WARNING "Could not find " 
                RGB_COLOR_BRIGHT_YELLOW 
                "%s " 
                ANSI_COLOR_RESET 
                "in " 
                ANSI_COLOR_CYAN 
                "%s" 
                ANSI_COLOR_RESET 
                ".",
                (MODE == MODE_TID) ? _prog_data->cmd_tokens[2] : _prog_data->cmd_tokens[3],
                _prog_data->active_project
        );
        return;
}

static int8_t targets_has_observed_ping(const TARGET * PETAL) {
        uint8_t ICMPV6_STATE;

        if (!PETAL || kscan_data_valid(PETAL) != ISTRUE) {
                return ISFALSE;
        }

        if (PETAL->SCAN.ICMPV4 & SCAN_RESULT_REPLY) {
                return ISTRUE;
        }

        ICMPV6_STATE = kscan_icmpv6_state(PETAL);
        return (ICMPV6_STATE & SCAN_RESULT_REPLY)
                ? ISTRUE
                : ISFALSE;
}

// @@ Host-level observation: true only when Kami has received a packet from this target
static int8_t targets_has_observation(
        _carry_forward * _prog_data,
        const unsigned char * TID,
        const TARGET * PETAL
) {
        if (!_prog_data || !TID || !PETAL) {
                return ISFALSE;
        }

        if (targets_has_observed_ping(PETAL) == ISTRUE) {
                return ISTRUE;
        }

        return kportdisplay_has_observed(_prog_data, TID);
}

// @@ Delete every target with no positive received-packet observation
void targets_del_unobserved(_carry_forward * _prog_data) {
        FLOWER * CURRENT;
        uint64_t DELETED = 0;
        uint64_t PRESERVED = 0;
        uint64_t ERRORS = 0;

        if (!_prog_data || !_prog_data->active_project_flower) {
                return;
        }

        CURRENT = _prog_data->active_project_flower->NEXT;

        while (CURRENT) {
                FLOWER * NEXT = CURRENT->NEXT;
                unsigned char TID[TID_BLOCK];
                char PATH[MAX_PATH];
                TARGET * PETAL;
                int8_t OBSERVED = ISFALSE;

                memset(TID, 0x00, sizeof(TID));
                memcpy(TID, CURRENT->TID, TID_BLOCK - 1);
                memset(PATH, 0x00, sizeof(PATH));

                PETAL = targets_buffer_target();
                if (!PETAL) {
                        ERRORS++;
                        CURRENT = NEXT;
                        continue;
                }

                if (
                        build_petal_path(
                                PATH,
                                sizeof(PATH),
                                _prog_data->wd,
                                _prog_data->active_project,
                                TID
                        ) != NORMAL
                        || targets_read_petal_data(PATH, PETAL) != NORMAL
                ) {
                        /* Preserve unreadable/corrupt state rather than deleting blindly. */
                        free(PETAL);
                        ERRORS++;
                        CURRENT = NEXT;
                        continue;
                }

                if (
                        targets_has_observation(
                                _prog_data,
                                TID,
                                PETAL
                        ) == ISTRUE
                ) {
                        OBSERVED = ISTRUE;
                }

                free(PETAL);

                if (OBSERVED == ISTRUE) {
                        PRESERVED++;
                        CURRENT = NEXT;
                        continue;
                }

                if (targets_delete_by_tid(_prog_data, TID) == NORMAL) {
                        DELETED++;
                } else {
                        ERRORS++;
                }

                CURRENT = NEXT;
        }

        if (_prog_data->active_project_target_count == 0) {
                _prog_data->active_project_has_targets = ISFALSE;
        }

        kui_add_line(
                NOTICE_SUCCESS
                "Deleted " ANSI_COLOR_CYAN "%llu" ANSI_COLOR_RESET
                " target%s with no received-packet observation.",
                (unsigned long long)DELETED,
                (DELETED == 1) ? "" : "s"
        );
        kui_add_line(
                NOTICE_INFO
                "Preserved " ANSI_COLOR_CYAN "%llu" ANSI_COLOR_RESET
                " observed target%s.",
                (unsigned long long)PRESERVED,
                (PRESERVED == 1) ? "" : "s"
        );

        if (ERRORS > 0) {
                kui_add_line(
                        NOTICE_WARNING
                        "Preserved " ANSI_COLOR_CYAN "%llu" ANSI_COLOR_RESET
                        " target%s because state could not be safely evaluated or deletion failed.",
                        (unsigned long long)ERRORS,
                        (ERRORS == 1) ? "" : "s"
                );
        }
}

// @@ Display all target data from the active project
void targets_display_from_project(_carry_forward * _prog_data) {
        FLOWER * CYCLER;
        char PATH[MAX_PATH];
        KPORTDISPLAY_MODE PORT_MODE = KPORTDISPLAY_MODE_COMPACT;
        int8_t OBSERVED_ONLY = ISFALSE;
        int8_t REQUIRE_OPEN_PORT = ISFALSE;
        unsigned int FILTER_PORT = 0U;
        KTARGETDISPLAY_ARGS ARGS = {0};

        if (
                !_prog_data
                ||
                !_prog_data->active_project_flower
        ) {
                return;
        }

        if (
                ktargetdisplay_parse_args(
                        _prog_data->cmd_tokens,
                        (size_t)_prog_data->cmd_tokens_count,
                        &ARGS
                ) != NORMAL
        ) {
                kui_add_line(NOTICE_WARNING "Invalid display option or port expression.");
                kui_add_line("< Usage: targets display [ -d | -o | -p <ports>... | -b <ports>... ]");
                return;
        }

        switch (ARGS.MODE) {
                case KTARGETDISPLAY_ARG_COMPACT:
                        break;

                case KTARGETDISPLAY_ARG_DETAIL_ALL:
                        PORT_MODE = KPORTDISPLAY_MODE_DETAIL_ALL;
                        break;

                case KTARGETDISPLAY_ARG_OBSERVED:
                        PORT_MODE = KPORTDISPLAY_MODE_OBSERVED;
                        OBSERVED_ONLY = ISTRUE;
                        break;

                case KTARGETDISPLAY_ARG_PORT_COMPACT:
                case KTARGETDISPLAY_ARG_PORT_DETAIL_OPEN:
                        REQUIRE_OPEN_PORT = ISTRUE;
                        PORT_MODE = ARGS.MODE == KTARGETDISPLAY_ARG_PORT_DETAIL_OPEN
                                ? KPORTDISPLAY_MODE_DETAIL_OPEN
                                : KPORTDISPLAY_MODE_COMPACT;

                        // Staged compatibility: preserve exact single-port behavior
                        // until bitmap selection/rendering is integrated in Phases 4-5.
                        if (ARGS.PORTS.count != 1U) {
                                kui_add_line(
                                        NOTICE_INFO
                                        "Port range/list syntax accepted; multi-port display "
                                        "is pending Patch 2 Phases 4-5."
                                );
                                return;
                        }

                        for (unsigned int PORT = 1U; PORT < MAX_PORTS; PORT++) {
                                if (kportspec_contains(&ARGS.PORTS, PORT) == ISTRUE) {
                                        FILTER_PORT = PORT;
                                        break;
                                }
                        }

                        if (FILTER_PORT == 0U) {
                                kui_add_line(NOTICE_WARNING "Invalid port selection.");
                                return;
                        }
                        break;
        }

        CYCLER = _prog_data->active_project_flower->NEXT;

        if (CYCLER == NULL) {
                kui_add_line(
                        NOTICE_WARNING "No targets to be displayed."
                );
                return;
        }

        kui_add_line("< Target Hosts:");
        kui_add_line("");

        for (; CYCLER != NULL; CYCLER = CYCLER->NEXT) {
                if (_prog_data->debug_flag == ISTRUE) {
                        kui_add_line_and_render(
                                "!["
                                ANSI_COLOR_MAGENTA
                                "targets_display_from_project()"
                                ANSI_COLOR_RESET
                                "]> Found TID: "
                                ANSI_COLOR_CYAN
                                "%s"
                                ANSI_COLOR_RESET
                                , (char*)CYCLER->TID
                        );
                }

                memset(PATH, 0x00, sizeof(PATH));

                if (
                        build_petal_path(
                                PATH,
                                sizeof(PATH),
                                _prog_data->wd,
                                _prog_data->active_project,
                                CYCLER->TID
                        ) != NORMAL
                ) {
                        kui_add_line(
                                NOTICE_ERROR "Failure to build target path for "
                                RGB_COLOR_RICH_RED
                                "%s"
                                ANSI_COLOR_RESET
                                "."
                                , CYCLER->TID
                        );
                        continue;
                }

                TARGET * TMP_TARGET = targets_buffer_target();

                if (!TMP_TARGET) {
                        kui_add_line(
                                NOTICE_ERROR "Failure to allocate target display buffer."
                        );
                        continue;
                }

                if (targets_read_petal_data(PATH, TMP_TARGET) != NORMAL) {
                        kui_add_line(
                                NOTICE_ERROR "Failure to read "
                                RGB_COLOR_RICH_RED
                                "%s"
                                ANSI_COLOR_RESET
                                , CYCLER->TID
                        );
                        kui_add_line(
                                NOTICE_INFO "Target data is corrupted or non-existent."
                        );
                        free(TMP_TARGET);
                        continue;
                }

                if (
                        OBSERVED_ONLY == ISTRUE
                        && targets_has_observation(
                                _prog_data,
                                CYCLER->TID,
                                TMP_TARGET
                        ) != ISTRUE
                ) {
                        free(TMP_TARGET);
                        continue;
                }

                if (
                        REQUIRE_OPEN_PORT == ISTRUE
                        && kportdisplay_has_open_tcp_port(
                                _prog_data,
                                CYCLER->TID,
                                FILTER_PORT
                        ) != ISTRUE
                ) {
                        free(TMP_TARGET);
                        continue;
                }

                targets_display_petal(
                        CYCLER->TID,
                        TMP_TARGET,
                        ISTRUE,
                        OBSERVED_ONLY
                );

                kportdisplay_target(
                        _prog_data,
                        CYCLER->TID,
                        PORT_MODE,
                        FILTER_PORT
                );

                free(TMP_TARGET);
                kui_add_line("");
        }

        // @@ Detailed project display is a two-pass render:
        //    1) all Kaminowaku built-ins for all targets;
        //    2) all stored Book data, grouped by target.
        if (PORT_MODE == KPORTDISPLAY_MODE_DETAIL_ALL) {
                CYCLER = _prog_data->active_project_flower->NEXT;

                while (CYCLER) {
                        char TARGET_DIRECTORY[MAX_PATH];

                        memset(
                                TARGET_DIRECTORY,
                                0x00,
                                sizeof(TARGET_DIRECTORY)
                        );
                        targets_buffer_path_stack_unsafe(
                                _prog_data,
                                MAX_PATH,
                                (const char*)CYCLER->TID,
                                TARGET_DIRECTORY
                        );

                        if (
                                book_output_directory_has_output(
                                        TARGET_DIRECTORY
                                ) == ISTRUE
                        ) {
                                kui_add_line(
                                        ANSI_COLOR_MAGENTA "[" ANSI_COLOR_RESET
                                        "BOOK DATA "
                                        ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET
                                        ANSI_COLOR_MAGENTA "]" ANSI_COLOR_RESET,
                                        CYCLER->TID
                                );

                                if (
                                        book_output_render_directory(
                                                TARGET_DIRECTORY
                                        ) == ABNORMAL
                                ) {
                                        kui_add_line(
                                                NOTICE_WARNING
                                                "Unable to render stored Book output for "
                                                ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET ".",
                                                CYCLER->TID
                                        );
                                }

                                kui_add_line("");
                        }

                        CYCLER = CYCLER->NEXT;
                }
        }
}

// @@ Display the active target context without repeating the TID banner
void targets_display_from_context(_carry_forward * _prog_data) {
        if (
                !_prog_data
                ||
                !_prog_data->active_project_active_target
                ||
                !_prog_data->active_project_active_target->PETAL
        ) {
                kui_add_line(
                        NOTICE_ERROR
                        "Target context data is unavailable."
                );
                return;
        }

        targets_display_petal(
                _prog_data->active_project_active_target->TID,
                _prog_data->active_project_active_target->PETAL,
                ISFALSE,
                ISFALSE
        );
        kportdisplay_target(
                _prog_data,
                _prog_data->active_project_active_target->TID,
                KPORTDISPLAY_MODE_DETAIL_OPEN,
                0U
        );

        if (
                !_prog_data->active_project_active_target_directory
                || book_output_render_directory(
                        _prog_data->active_project_active_target_directory
                ) == ABNORMAL
        ) {
                kui_add_line(
                        NOTICE_WARNING
                        "Unable to render stored Book output for active target."
                );
        }
}

// @@ Match on identifier, progress into subcommands if exists
void targets_context(_carry_forward * _prog_data, int8_t MODE) {
        if (_prog_data->debug_flag == ISTRUE) {
                kui_add_line_and_render(
                       "!["
                       ANSI_COLOR_MAGENTA
                       "targets_context_match_on_identifier()"
                       ANSI_COLOR_RESET
                       "]> "
                       ANSI_COLOR_CYAN
                       "MODE "
                       ANSI_COLOR_RESET
                       "= "
                       ANSI_COLOR_CYAN
                       "%d"
                       ANSI_COLOR_RESET
                       , MODE
                );
        }
        fbvt_clear();
        const unsigned char * TMP_TID = NULL;
        if (MODE == MODE_IDENTIFIER) {
                TMP_TID = targets_find_by_value_return_tid(_prog_data, _prog_data->cmd_tokens[1][1], _prog_data->cmd_tokens[2], PRESERVE);
                if (TMP_TID) {
                        MODE = MODE_TID;
                }
        } else {
                if (_prog_data->debug_flag == ISTRUE) {
                        kui_add_line_and_render(
                                "!["
                                ANSI_COLOR_MAGENTA
                                "targets_context()"
                                ANSI_COLOR_RESET
                                "]> Attempting to validate TID."
                        );
                }
                if (vtid(_prog_data->cmd_tokens[1]) == NORMAL) {
                        if (_prog_data->debug_flag == ISTRUE) {
                                kui_add_line_and_render(
                                        "!["
                                        ANSI_COLOR_MAGENTA
                                        "targets_context()"
                                        ANSI_COLOR_RESET
                                        "]> TID validation success: "
                                        ANSI_COLOR_CYAN
                                        "%s"
                                        ANSI_COLOR_RESET
                                        , _prog_data->cmd_tokens[1]
                                );
                        }
                        TMP_TID = _prog_data->cmd_tokens[1];
                        char PATH[PATH_MAX];
                        memset(PATH, 0x00, PATH_MAX);
                        build_petal_path(PATH, PATH_MAX, _prog_data->wd, _prog_data->active_project, TMP_TID);
                        fbvt_set(TMP_TID, PATH);

                } else {
                        kui_add_line(
                                NOTICE_WARNING
                                RGB_COLOR_BRIGHT_YELLOW "%s" ANSI_COLOR_RESET " is invalid."
                                , _prog_data->cmd_tokens[1]
                        );
                        kui_add_line(
                                NOTICE_INFO "Without -[U|4|6|M] specified, a valid "
                                ANSI_COLOR_CYAN
                                "TID"
                                ANSI_COLOR_RESET
                                " must be provided."
                        );
                        return;
                }
        }
        if (MODE == MODE_TID) {
                if (_prog_data->debug_flag == ISTRUE) {
                        kui_add_line_and_render(
                                "!["
                                ANSI_COLOR_MAGENTA
                                "targets_context()"
                                ANSI_COLOR_RESET
                                "]> Ready for processing."
                        );
                }
                FLOWER *PREV = NULL;
                FLOWER *NODE = NULL;
                NODE = targets_find_by_tid_return_flower_node(_prog_data, TMP_TID, &PREV);
                if (NODE) {
                        kui_add_line(
                                NOTICE_SUCCESS "Loading "
                                ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET " from "
                                ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET "."
                                , TMP_TID
                                , _prog_data->active_project
                        );
                        if (_prog_data->debug_flag == ISTRUE) {
                                kui_add_line_and_render(
                                        "!["
                                        ANSI_COLOR_MAGENTA
                                        "targets_context()"
                                        ANSI_COLOR_RESET
                                        "]> FBVT pre-context switch: "
                                        ANSI_COLOR_CYAN
                                        "%s"
                                        ANSI_COLOR_RESET
                                        ", "
                                        ANSI_COLOR_CYAN
                                        "%s"
                                        ANSI_COLOR_RESET
                                        , fbvt_tid()
                                        , fbvt_path()
                                );
                        }
                        targets_load_petal_data(fbvt_path(), NODE);
                        if (!NODE->PETAL) {
                                kui_add_line(
                                        NOTICE_ERROR
                                        "Target context data could not be loaded."
                                );
                                return;
                        }

                        _prog_data->active_project_active_target = NODE;
                        _prog_data->active_project_active_target_context = ISTRUE;

                        if (_prog_data->debug_flag == ISTRUE) {
                                kui_add_line_and_render(
                                        "!["
                                        ANSI_COLOR_MAGENTA
                                        "targets_context()"
                                        ANSI_COLOR_RESET
                                        "]> " 
                                        ANSI_COLOR_CYAN 
                                        "%s" 
                                        ANSI_COLOR_RESET 
                                        " @ " 
                                        ANSI_COLOR_YELLOW 
                                        "%p"
                                        ANSI_COLOR_RESET
                                        " -> "
                                        ANSI_COLOR_YELLOW
                                        "%p"
                                        ANSI_COLOR_RESET
                                        , TMP_TID
                                        , NODE
                                        , NODE->PETAL 
                                );
                                 kui_add_line_and_render(
                                        "!["
                                        ANSI_COLOR_MAGENTA
                                        "targets_context()"
                                        ANSI_COLOR_RESET
                                        "]> "
                                        ANSI_COLOR_CYAN
                                        "_prog_data->active_project_active_target"
                                        ANSI_COLOR_RESET
                                        " @ " 
                                        ANSI_COLOR_YELLOW
                                        "%p"
                                        ANSI_COLOR_RESET
                                        , _prog_data->active_project_active_target
                                 );
                        }
                        memset(_prog_data->prompt, 0x00, DOUBLE_BLOCK);
                        strcats(
                                (char*)_prog_data->prompt, 
                                sizeof(_prog_data->prompt), 
                                "[" ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET "]> "
                                , TMP_TID
                        );
                        _prog_data->active_project_active_target_directory = (char*)calloc(1, MAX_PATH);
                        if (!_prog_data->active_project_active_target_directory) {
                                kui_add_line(
                                        NOTICE_ERROR
                                        "Failed to allocate target context path."
                                );
                                free(NODE->PETAL);
                                NODE->PETAL = NULL;
                                _prog_data->active_project_active_target = NULL;
                                _prog_data->active_project_active_target_context = ISFALSE;
                                memset(_prog_data->prompt, 0x00, DOUBLE_BLOCK);
                                strcats(
                                        (char*)_prog_data->prompt,
                                        sizeof(_prog_data->prompt),
                                        "> "
                                );
                                return;
                        }

                        targets_buffer_path_stack_unsafe(
                                _prog_data,
                                MAX_PATH,
                                fbvt_tid(),
                                _prog_data->active_project_active_target_directory
                        );
                        _prog_data->f_type = C_TARGETS_CONTEXTUAL;
                        return;
                }
        }
        kui_add_line(
                NOTICE_WARNING "Could not find " 
                RGB_COLOR_BRIGHT_YELLOW 
                "%s " 
                ANSI_COLOR_RESET 
                "in " 
                ANSI_COLOR_CYAN 
                "%s" 
                ANSI_COLOR_RESET 
                ".",
                (MODE == MODE_TID) ? _prog_data->cmd_tokens[1] : _prog_data->cmd_tokens[2],
                _prog_data->active_project
        );
        return;
}

// @@ Set params while in target context
void targets_set_identifier(_carry_forward * _prog_data) {
        if (_prog_data->cmd_tokens_count < 3) {
                kui_add_line(
                        NOTICE_INFO "Usage: set [-U46MN] " ANSI_COLOR_CYAN "<Value>" ANSI_COLOR_RESET
                );
                kui_add_line(
                        NOTICE_INFO "The -N must be last."
                );
                return;
        }
        int8_t ALL_DATA_VALID = ISTRUE;
        int8_t ALL_PARAMS_VALID = ISTRUE;
        int8_t URL  = ISFALSE;
        int8_t IP4  = ISFALSE;
        int8_t IP6  = ISFALSE;
        int8_t MAC  = ISFALSE;
        int8_t NOTE = ISFALSE;
        for (unsigned long i = 1; i < _prog_data->cmd_tokens_count; i++) {
                if (
                        ((i - 1) % 2 == 0)
                        &&
                        (_prog_data->cmd_tokens_count > (i + 1))
                ) {
                        if (_prog_data->debug_flag == ISTRUE) {
                                kui_add_line_and_render(
                                        "!["
                                        ANSI_COLOR_MAGENTA
                                        "targets_set_identifier()"
                                        ANSI_COLOR_RESET
                                        "]> Loop tracker: "
                                        ANSI_COLOR_CYAN
                                        "%lu"
                                        ANSI_COLOR_RESET
                                        , i
                                );
                        }
                        switch(_prog_data->cmd_tokens[i][1]) {
                                case 0x55: {
                                        if (!URL) {
                                                if (
                                                        targets_modify_petal_url(
                                                                _prog_data,
                                                                _prog_data->active_project_active_target->PETAL,
                                                                _prog_data->cmd_tokens[i+1]
                                                        ) != ABNORMAL
                                                ) {
                                                        URL = ISTRUE;
                                                } else {
                                                        ALL_DATA_VALID = ISFALSE;
                                                }
                                        }
                                        break;
                                }
                                case 0x34: {
                                        if (!IP4) {
                                                if (
                                                        targets_modify_petal_ipv4(
                                                                _prog_data,
                                                                _prog_data->active_project_active_target->PETAL,
                                                                _prog_data->cmd_tokens[i+1]
                                                        ) != ABNORMAL
                                                ) {
                                                        IP4 = ISTRUE;
                                                } else {
                                                        ALL_DATA_VALID = ISFALSE;
                                                }
                                        }
                                        break;
                                }
                                case 0x36: {
                                        if (!IP6) {
                                                if (
                                                        targets_modify_petal_ipv6(
                                                                _prog_data,
                                                                _prog_data->active_project_active_target->PETAL,
                                                                _prog_data->cmd_tokens[i+1]
                                                        ) != ABNORMAL
                                                ) {
                                                        IP6 = ISTRUE;
                                                } else {
                                                        ALL_DATA_VALID = ISFALSE;
                                                }
                                        }
                                        break;
                                }
                                case 0x4D: {
                                        if (!MAC) {
                                                if (
                                                        targets_modify_petal_mac(
                                                                _prog_data,
                                                                _prog_data->active_project_active_target->PETAL,
                                                                _prog_data->cmd_tokens[i+1]
                                                        ) !=ABNORMAL
                                                ) {
                                                        MAC = ISTRUE;
                                                } else {
                                                        ALL_DATA_VALID = ISFALSE;
                                                }
                                        }
                                        break;
                                }
                                case 0x4E: {
                                        if (!NOTE) {
                                                char * NOTE_START;
                                                char * NOTE_END;
                                                char * TRAILING;
                                                size_t NOTE_LENGTH;
                                                unsigned char NOTE_VALUE[NOTE_BLOCK];

                                                NOTE_START = strchr(
                                                        (char*)_prog_data->cmd_input,
                                                        '\"'
                                                );
                                                NOTE_END = NOTE_START
                                                        ? strchr(NOTE_START + 1, '\"')
                                                        : NULL;

                                                if (
                                                        !NOTE_START
                                                        || !NOTE_END
                                                        || NOTE_END == NOTE_START + 1
                                                ) {
                                                        kui_add_line(
                                                                NOTICE_WARNING
                                                                "Invalid note format, use \" to delineate a non-empty note."
                                                        );
                                                        ALL_DATA_VALID = ISFALSE;
                                                } else {
                                                        TRAILING = NOTE_END + 1;
                                                        while (
                                                                *TRAILING == ' '
                                                                || *TRAILING == '\t'
                                                        ) {
                                                                TRAILING++;
                                                        }

                                                        if (*TRAILING != 0x00) {
                                                                kui_add_line(
                                                                        NOTICE_WARNING
                                                                        "The -N note must be the final set parameter."
                                                                );
                                                                ALL_PARAMS_VALID = ISFALSE;
                                                        } else {
                                                                NOTE_LENGTH = (size_t)(
                                                                        NOTE_END - NOTE_START - 1
                                                                );

                                                                if (NOTE_LENGTH >= sizeof(NOTE_VALUE)) {
                                                                        kui_add_line(
                                                                                NOTICE_WARNING
                                                                                "Target note exceeds the maximum note length."
                                                                        );
                                                                        ALL_DATA_VALID = ISFALSE;
                                                                } else {
                                                                        memset(
                                                                                NOTE_VALUE,
                                                                                0x00,
                                                                                sizeof(NOTE_VALUE)
                                                                        );
                                                                        memcpy(
                                                                                NOTE_VALUE,
                                                                                NOTE_START + 1,
                                                                                NOTE_LENGTH
                                                                        );

                                                                        if (
                                                                                targets_modify_petal_note(
                                                                                        _prog_data,
                                                                                        _prog_data->active_project_active_target->PETAL,
                                                                                        NOTE_VALUE
                                                                                ) != ABNORMAL
                                                                        ) {
                                                                                NOTE = ISTRUE;
                                                                        } else {
                                                                                ALL_DATA_VALID = ISFALSE;
                                                                        }
                                                                }
                                                        }
                                                }
                                        }

                                        // @@ -N consumes the quoted remainder of cmd_input.
                                        // Quoted note words may exist as command tokens and
                                        // must never be reinterpreted as additional flags.
                                        i = _prog_data->cmd_tokens_count;
                                        break;
                                }
                                default: {
                                        ALL_PARAMS_VALID = ISFALSE;
                                        break;
                                }
                        }
                }
        }
        if (ALL_DATA_VALID == ISTRUE && ALL_PARAMS_VALID == ISTRUE) {
                if (targets_write_petal_data(fbvt_path(), _prog_data->active_project_active_target->PETAL) != NORMAL) {
                        kui_add_line( 
                                NOTICE_ERROR "Failure to write target data."
                        );
                }
        } else {
                if (targets_read_petal_data(fbvt_path(), _prog_data->active_project_active_target->PETAL) == ABNORMAL) {
                        kui_add_line( 
                                NOTICE_ERROR "Failure to repopulate target data."
                        );
                }
                kui_add_line(
                        NOTICE_NOPE "Will not set target with invalid parameters."
                );
                kui_add_line(
                        NOTICE_INFO "Usage: set [-U46MN] " ANSI_COLOR_CYAN "<Value>" ANSI_COLOR_RESET
                );
                kui_add_line(
                        NOTICE_INFO "The -N must be last."
                );
        }
}