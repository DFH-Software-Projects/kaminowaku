// Copyright 2026 Jamison A. Drapeau
#include "tlib.h"
#include "helpers.h"
#include "kui.h"
#include "kscan.h"
#include <fcntl.h>
#include <time.h>
#include <stdio.h>
#include <regex.h>
#include <limits.h>
#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/mman.h>

// @@ Data-Path building helper function (.data included)
int build_petal_path(char *out, c_size_t outsz, const unsigned char *wd, const unsigned char *project, const unsigned char *tid) {
        if (!out || outsz == 0 || !wd || !project || !tid) return -1;
        int n = snprintf(
                out, outsz, "%s/%s%s/%s/.data", (const char*)wd, KAMI_USER_PROJECTS_DIR, (const char *)project, (const char *)tid
        );
        return (n < 0 || (size_t)n >= outsz) ? -1 : 0;
}

// @@ Buff target path (Non-Data)
void targets_buffer_path_stack_unsafe(_carry_forward * _prog_data, int SIZE, const char * TID, char * PATH_BUFFER) {
                memset(PATH_BUFFER, 0x00, SIZE);
                strcats(PATH_BUFFER, SIZE, (char*)_prog_data->wd);
                strcats(PATH_BUFFER, SIZE, KAMI_USER_PROJECTS_DIR);
                strcats(PATH_BUFFER, SIZE, (char*)_prog_data->active_project);
                strcats(PATH_BUFFER, SIZE, "/");
                strcats(PATH_BUFFER, SIZE, TID);
}

//===================================================================================================[Low Level Helpers]

// @@ Save target data
int targets_write_petal_data(const char * SAVE_DATA, const TARGET * TARGET_DATA) {
        if (!TARGET_DATA) {
                return ABNORMAL;
        }
        FILE * SAVE_DATA_FILE = fopen(SAVE_DATA, "w+b");
        if (!SAVE_DATA_FILE) {
                return ABNORMAL;
        }
        TARGET TMP = *TARGET_DATA;
        if (!fwrite(&TMP, sizeof(TARGET), 1, SAVE_DATA_FILE)) {
                fclose(SAVE_DATA_FILE);
                return ABNORMAL;
        }
        fflush(SAVE_DATA_FILE);
        fclose(SAVE_DATA_FILE);
        return 0;
}

// @@ Read one exact persisted target record
int targets_read_petal_data(const char * SAVE_DATA, TARGET * FILL_TARGET_DATA) {
        struct stat st;
        ssize_t READ_LENGTH;
        int SDFD;

        if (!SAVE_DATA || !FILL_TARGET_DATA) {
                return ABNORMAL;
        }

        if (
                stat(SAVE_DATA, &st) != MATCH
                || st.st_size != (off_t)sizeof(TARGET)
        ) {
                return ABNORMAL;
        }

        SDFD = open(SAVE_DATA, O_RDONLY);
        if (SDFD < NORMAL) {
                return ABNORMAL;
        }

        READ_LENGTH = read(
                SDFD,
                FILL_TARGET_DATA,
                sizeof(TARGET)
        );

        if (READ_LENGTH != (ssize_t)sizeof(TARGET)) {
                close(SDFD);
                return ABNORMAL;
        }

        close(SDFD);
        return NORMAL;
}

// @@ Buff target
TARGET * targets_buffer_target(void) {
        TARGET * T_TARGET = calloc(1, sizeof(TARGET));

        if (!T_TARGET) {
                return NULL;
        }

        return T_TARGET;
}

// @@ Load a target petal
void targets_load_petal_data(const char * SAVE_DATA, FLOWER * ENTRY_POINT) {
        if (!SAVE_DATA || !ENTRY_POINT) {
                return;
        }

        ENTRY_POINT->PETAL = targets_buffer_target();
        if (!ENTRY_POINT->PETAL) {
                kui_add_line(
                        NOTICE_ERROR "Failed to allocate target data."
                );
                return;
        }

        if (targets_read_petal_data(SAVE_DATA, ENTRY_POINT->PETAL) == ABNORMAL) {
                kui_add_line(
                        NOTICE_ERROR "Failed to load target data into memory."
                );
                free(ENTRY_POINT->PETAL);
                ENTRY_POINT->PETAL = NULL;
        }
}

// @@ Free all nodes not including anchor
void targets_free_nodes(FLOWER *ANCHOR) {
        FLOWER * CURRENT_FLOWER_NODE_INDEX = ANCHOR->NEXT;
        ANCHOR->NEXT = NULL;
        while (CURRENT_FLOWER_NODE_INDEX) {
                FLOWER * NEXT = CURRENT_FLOWER_NODE_INDEX->NEXT;
                if (CURRENT_FLOWER_NODE_INDEX->PETAL) {
                        free(CURRENT_FLOWER_NODE_INDEX->PETAL);
                        CURRENT_FLOWER_NODE_INDEX->PETAL = NULL;
                }
                free(CURRENT_FLOWER_NODE_INDEX);
                CURRENT_FLOWER_NODE_INDEX = NEXT;
        }
        return;
}


//===================================================================================================[Support Functions]

// @@ Create FLOWER Node
FLOWER* flower_create_node(FLOWER *ANCHOR_NODE, char * PASS_IN_TID) {
        FLOWER *new_FLOWER_NODE = malloc(sizeof(FLOWER));
        if (!new_FLOWER_NODE) {
                return NULL;
        }
        FLOWER * CURRENT_FLOWER_NODE_INDEX = ANCHOR_NODE;
        while (CURRENT_FLOWER_NODE_INDEX->NEXT) {
                CURRENT_FLOWER_NODE_INDEX = CURRENT_FLOWER_NODE_INDEX->NEXT;
        }
        memcpy(new_FLOWER_NODE->TID, PASS_IN_TID, TID_BLOCK);
        new_FLOWER_NODE->FD = FD_RESET;
        new_FLOWER_NODE->LAST_MAP = RESET;
        new_FLOWER_NODE->MAP_SIZE = RESET;
        new_FLOWER_NODE->MAP_ADDRESS = NULL;
        new_FLOWER_NODE->PETAL = NULL;
        new_FLOWER_NODE->NEXT = NULL;
        CURRENT_FLOWER_NODE_INDEX->NEXT = new_FLOWER_NODE;
        return new_FLOWER_NODE;
}

// @@ Set target URL
int targets_modify_petal_url(_carry_forward * _prog_data, TARGET * MODIFY_PETAL, unsigned char * ADD_VALUE) {
        if (_prog_data->debug_flag == ISTRUE) {
                kui_add_line_and_render(
                        "![" ANSI_COLOR_MAGENTA "targets_modify_petal_url()" ANSI_COLOR_RESET "]> Attempting to modify URL of target."
                );
        }
        memset(MODIFY_PETAL->URL, 0x00, URL_BLOCK);
        strcats((char*)MODIFY_PETAL->URL, sizeof(MODIFY_PETAL->URL), (char*)ADD_VALUE);
        kui_add_line(
                NOTICE_INFO
                ANSI_COLOR_YELLOW "WARNING" ANSI_COLOR_RESET ": Kaminowaku does not do URL validation."
        );
        kui_add_line(
                NOTICE_SUCCESS "Target URL"
                ANSI_COLOR_CYAN " %s " ANSI_COLOR_RESET
                "successfully set."
                , (char*)ADD_VALUE
        );
        return 1;
}

// @@ Set target IPv4
int targets_modify_petal_ipv4(_carry_forward * _prog_data, TARGET * MODIFY_PETAL, unsigned char * ADD_VALUE) {
        if (_prog_data->debug_flag == ISTRUE) {
                kui_add_line_and_render(
                        "![" ANSI_COLOR_MAGENTA "targets_modify_petal_ipv4()" ANSI_COLOR_RESET "]> Attempting to modify IPv4 of target."
                );
        }
        const char *pattern = "^(([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])\\.){3}"
                              "([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])$";
        regex_t regex;
        int result;
        if (regcomp(&regex, pattern, REG_EXTENDED | REG_NOSUB) != 0) {
                kui_add_line(
                        NOTICE_ERROR "Failure to validate IPv4 address."
                );
                return -1;
        }
        result = regexec(&regex, (char*)ADD_VALUE, 0, NULL, 0);
        regfree(&regex);
        if (result != 0) {
                kui_add_line(
                        NOTICE_WARNING
                        RGB_COLOR_BRIGHT_YELLOW " %s " ANSI_COLOR_RESET
                        "is not a valid IPv4 address."
                        , (char*)ADD_VALUE
                );
                return -1;
        } else {
                memset(MODIFY_PETAL->IPV4, 0x00, IPV4_BLOCK);
                strcats((char*)MODIFY_PETAL->IPV4, sizeof(MODIFY_PETAL->IPV4), (char*)ADD_VALUE);
                kui_add_line(
                        NOTICE_SUCCESS "Target IPv4"
                        ANSI_COLOR_CYAN " %s " ANSI_COLOR_RESET
                        "successfully set."
                        , (char*)ADD_VALUE
                );
                return 1;
        }
}

// @@ Set target IPv6
int targets_modify_petal_ipv6(_carry_forward * _prog_data, TARGET * MODIFY_PETAL, unsigned char * ADD_VALUE) {
        if (_prog_data->debug_flag == ISTRUE) {
                kui_add_line_and_render(
                        "![" ANSI_COLOR_MAGENTA "targets_modify_petal_ipv6()" ANSI_COLOR_RESET "]> Attempting to modify IPv6 of target."
                );
        }
        const char *pattern =
                "^("
                "([0-9A-Fa-f]{1,4}:){7}[0-9A-Fa-f]{1,4}|"
                "([0-9A-Fa-f]{1,4}:){1,7}:|"
                "([0-9A-Fa-f]{1,4}:){1,6}:[0-9A-Fa-f]{1,4}|"
                "([0-9A-Fa-f]{1,4}:){1,5}(:[0-9A-Fa-f]{1,4}){1,2}|"
                "([0-9A-Fa-f]{1,4}:){1,4}(:[0-9A-Fa-f]{1,4}){1,3}|"
                "([0-9A-Fa-f]{1,4}:){1,3}(:[0-9A-Fa-f]{1,4}){1,4}|"
                "([0-9A-Fa-f]{1,4}:){1,2}(:[0-9A-Fa-f]{1,4}){1,5}|"
                "[0-9A-Fa-f]{1,4}:((:[0-9A-Fa-f]{1,4}){1,6})|"
                ":((:[0-9A-Fa-f]{1,4}){1,7}|:)|"
                "fe80:(:[0-9A-Fa-f]{0,4}){0,4}%[0-9A-Za-z]{1,}|"
                "::(ffff(:0{1,4}){0,1}:){0,1}"
                "((25[0-5]|(2[0-4]|1{0,1}[0-9])?[0-9])\\.){3,3}"
                "(25[0-5]|(2[0-4]|1{0,1}[0-9])?[0-9])|"
                "([0-9A-Fa-f]{1,4}:){1,4}:"
                "((25[0-5]|(2[0-4]|1{0,1}[0-9])?[0-9])\\.){3,3}"
                "(25[0-5]|(2[0-4]|1{0,1}[0-9])?[0-9])"
                ")$";
        regex_t regex;
        int result;
        if (regcomp(&regex, pattern, REG_EXTENDED | REG_NOSUB) != 0) {
                kui_add_line(
                        NOTICE_ERROR "Failure to validate IPv6 address."
                );
                return -1;
        }
        result = regexec(&regex, (char*)ADD_VALUE, 0, NULL, 0);
        regfree(&regex);
        if (result != 0) {
                 kui_add_line(
                        NOTICE_WARNING
                        RGB_COLOR_BRIGHT_YELLOW " %s " ANSI_COLOR_RESET
                        "is not a valid IPv6 address."
                        , (char*)ADD_VALUE
                );
                return -1;
        } else {
                memset(MODIFY_PETAL->IPV6, 0x00, IPV6_BLOCK);
                strcats((char*)MODIFY_PETAL->IPV6, sizeof(MODIFY_PETAL->IPV6), (char*)ADD_VALUE);
                kui_add_line(
                        NOTICE_SUCCESS "Target IPv6"
                        ANSI_COLOR_CYAN " %s " ANSI_COLOR_RESET
                        "successfully set."
                        , (char*)ADD_VALUE
                );
                return 1;
        }
}

// @@ Set target MAC
int targets_modify_petal_mac(_carry_forward * _prog_data, TARGET * MODIFY_PETAL, unsigned char * ADD_VALUE) {
        if (_prog_data->debug_flag == ISTRUE) {
                kui_add_line_and_render(
                        "![" ANSI_COLOR_MAGENTA "targets_modify_petal_mac()" ANSI_COLOR_RESET "]> Attempting to modify MAC of target."
                );
        }
        const char *pattern = "^([0-9A-Fa-f]{2}([-:])){5}([0-9A-Fa-f]{2})$";
        regex_t regex;
        int result;
        if (regcomp(&regex, pattern, REG_EXTENDED | REG_NOSUB) != 0) {
                kui_add_line(
                        NOTICE_ERROR "Failure to validate MAC address."
                );
                return -1;
        }
        result = regexec(&regex, (char*)ADD_VALUE, 0, NULL, 0);
        regfree(&regex);
        if (result != 0) {
                 kui_add_line(
                        NOTICE_WARNING
                        RGB_COLOR_BRIGHT_YELLOW " %s " ANSI_COLOR_RESET
                        "is not a valid MAC address."
                        , (char*)ADD_VALUE
                );
                return -1;
        } else {
                memset(MODIFY_PETAL->MAC, 0x00, MAC_BLOCK);
                strcats((char*)MODIFY_PETAL->MAC, sizeof(MODIFY_PETAL->MAC), (char*)ADD_VALUE);
                kui_add_line(
                        NOTICE_SUCCESS "Target MAC"
                        ANSI_COLOR_CYAN " %s " ANSI_COLOR_RESET
                        "successfully set."
                        , (char*)ADD_VALUE
                );
                return 1;
        }
}

// @@ Set target URL
int targets_modify_petal_note(_carry_forward * _prog_data, TARGET * MODIFY_PETAL, unsigned char * ADD_VALUE) {
        if (_prog_data->debug_flag == ISTRUE) {
                kui_add_line_and_render(
                        "![" ANSI_COLOR_MAGENTA "targets_modify_petal_note()" ANSI_COLOR_RESET "]> Attempting to modify note of target."
                );
        }
        memset(MODIFY_PETAL->NOTE, 0x00, NOTE_BLOCK);
        strcats((char*)MODIFY_PETAL->NOTE, sizeof(MODIFY_PETAL->NOTE), (char*)ADD_VALUE);
        kui_add_line(
                NOTICE_SUCCESS "Target note"
                ANSI_COLOR_CYAN " '%s' " ANSI_COLOR_RESET
                "successfully set."
                , (char*)ADD_VALUE
        );
        return 1;
}

// @@ Find FLOWER node by TID; optionally returns PREV via PREV_OUT (may be NULL)
FLOWER * targets_find_by_tid_return_flower_node(_carry_forward * _prog_data, const unsigned char *TID, FLOWER **PREV_OUT) {
        if (!_prog_data || !_prog_data->active_project_flower) {
                if (PREV_OUT) *PREV_OUT = NULL;
                return NULL;
        }
        FLOWER *PREV    = _prog_data->active_project_flower;
        FLOWER *CURRENT = PREV->NEXT;
        if (CURRENT == NULL) {return NULL;}
        while (CURRENT) {
                if (memcmp(CURRENT->TID, TID, TID_BLOCK - 1) == NORMAL) {
                        if (PREV_OUT) *PREV_OUT = PREV;
                        if (_prog_data->debug_flag == ISTRUE) {
                               kui_add_line_and_render(
                                        "!["
                                        ANSI_COLOR_MAGENTA
                                        "targets_find_by_tid_return_flower_node()"
                                        ANSI_COLOR_RESET
                                        "]> " ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET " @ " ANSI_COLOR_YELLOW "%p" ANSI_COLOR_RESET,
                                        TID, CURRENT 
                                );
                        }
                        return CURRENT;
                }
                PREV    = CURRENT;
                CURRENT = CURRENT->NEXT;
        }
        if (PREV_OUT) *PREV_OUT = NULL;
        return NULL;
}

// @@ Remove all the things
static int rmdir_recursive(const char *PATH) {
        DIR *d = opendir(PATH);
        if (!d) return unlink(PATH);
        struct dirent *e;
        char child[PATH_MAX];
        while ((e = readdir(d)) != NULL) {
                if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) {continue;}
                snprintf(child, sizeof(child), "%s/%s", PATH, e->d_name);
                if (rmdir_recursive(child) != 0) {
                        closedir(d);
                        return ABNORMAL;
                }
        }
        closedir(d);
        return rmdir(PATH);
}

// @@ Delete target by TID: close/unmap, remove on-disk dir, unlink node
int targets_delete_by_tid(_carry_forward * _prog_data, const unsigned char * TID) {
        FLOWER *PREV = NULL;
        FLOWER *NODE = targets_find_by_tid_return_flower_node(_prog_data, TID, &PREV);
        if (!NODE) {
                return ABNORMAL;
        }
        if (NODE->MAP_ADDRESS) {
                munmap(NODE->MAP_ADDRESS, NODE->MAP_SIZE);
                NODE->MAP_ADDRESS = NULL;
                NODE->MAP_SIZE = RESET;
        }
        if (NODE->FD >= 0) {
                close(NODE->FD);
                NODE->FD = FD_RESET;
        }
        char PATH[PATH_MAX];
        snprintf(PATH, sizeof(PATH), "%s%s%s/%s", _prog_data->wd, KAMI_USER_PROJECTS_DIR, _prog_data->active_project, NODE->TID);
        if (_prog_data->debug_flag == ISTRUE) {
                kui_add_line_and_render(
                        "!["
                        ANSI_COLOR_MAGENTA
                        "targets_delete_by_tid()"
                        ANSI_COLOR_RESET
                        "]> Path marked for recursive deletion is " ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET, PATH
                );
        }
        if (rmdir_recursive(PATH) != NORMAL) {
                return ABNORMAL;
        }
        if (!PREV) return ABNORMAL;
        if (_prog_data->debug_flag == ISTRUE) {
                kui_add_line_and_render(
                        "!["
                        ANSI_COLOR_MAGENTA
                        "targets_delete_by_tid()"
                        ANSI_COLOR_RESET
                        "]> Unlinking NODE @ "
                        ANSI_COLOR_YELLOW
                        "%p"
                        ANSI_COLOR_RESET
                        , NODE
                );
                kui_add_line_and_render(
                        "!["
                        ANSI_COLOR_MAGENTA
                        "targets_delete_by_tid()"
                        ANSI_COLOR_RESET
                        "]> Altering PREV @ "
                        ANSI_COLOR_YELLOW
                        "%p"
                        ANSI_COLOR_RESET
                        , PREV
                );
                kui_add_line_and_render(
                        "!["
                        ANSI_COLOR_MAGENTA
                        "targets_delete_by_tid()"
                        ANSI_COLOR_RESET
                        "]> PREV->NEXT @ "
                        ANSI_COLOR_YELLOW
                        "%p"
                        ANSI_COLOR_RESET
                        " relinking to NODE->NEXT @ "
                        ANSI_COLOR_YELLOW
                        "%p"
                        ANSI_COLOR_RESET
                        , PREV->NEXT
                        , NODE->NEXT 
                );
        }
        PREV->NEXT = NODE->NEXT;
        free(NODE);
        if (_prog_data->active_project_target_count > 0) _prog_data->active_project_target_count--;
        return NORMAL;
}

// @@ Internal display color selection for persisted ICMPv4 state
static const char * targets_display_icmpv4_color(uint8_t RESULT) {
        if (RESULT & SCAN_RESULT_REPLY) {
                return RGB_COLOR_BRIGHT_GREEN;
        }
        if (RESULT & SCAN_RESULT_TIMEOUT) {
                return RGB_COLOR_BRIGHT_YELLOW;
        }
        if (RESULT & SCAN_RESULT_ROUTE) {
                return RGB_COLOR_RICH_RED;
        }
        if (RESULT & SCAN_RESULT_NEIGHBOR) {
                return RGB_COLOR_ORANGE;
        }
        if (RESULT & SCAN_RESULT_ERROR) {
                return RGB_COLOR_RICH_RED;
        }
        return RGB_COLOR_SOFT_GREY;
}

// @@ Reusable target renderer for project and target-context display paths
void targets_display_petal(
        const unsigned char * TID,
        const TARGET * PETAL,
        int8_t DISPLAY_TID,
        int8_t OBSERVED_ONLY
) {
        char SCAN_TIME[64];
        char ICMP_TIME[64];
        uint8_t ICMPV6_STATE;
        uint64_t ICMPV6_SCAN_NS;

        if (!PETAL) {
                return;
        }

        if (DISPLAY_TID == ISTRUE) {
                if (!TID) {
                        return;
                }

                kui_add_line(ANSI_COLOR_CYAN "┏━━━━━━━━━━━━━━━━━━┓");
                kui_add_line("┃" ANSI_COLOR_RESET " %s " ANSI_COLOR_CYAN "┃", TID);
                kui_add_line("┗━━━━━━━━━━━━━━━━━━┛" ANSI_COLOR_RESET);

                if (PETAL->URL[0] != 0x00) {
                        kui_add_line(ANSI_COLOR_CYAN "    URL" ANSI_COLOR_RESET ":\t%s", PETAL->URL);
                } else {
                        kui_add_line(ANSI_COLOR_CYAN "    URL" ANSI_COLOR_RESET ":\t" RGB_COLOR_BRIGHT_YELLOW "Not Set" ANSI_COLOR_RESET);
                }

                if (PETAL->IPV4[0] != 0x00) {
                        kui_add_line(ANSI_COLOR_CYAN "   IPv4" ANSI_COLOR_RESET ":\t%s", PETAL->IPV4);
                } else {
                        kui_add_line(ANSI_COLOR_CYAN "   IPv4" ANSI_COLOR_RESET ":\t" RGB_COLOR_BRIGHT_YELLOW "Not Set" ANSI_COLOR_RESET);
                }

                if (PETAL->IPV6[0] != 0x00) {
                        kui_add_line(ANSI_COLOR_CYAN "   IPv6" ANSI_COLOR_RESET ":\t%s", PETAL->IPV6);
                } else {
                        kui_add_line(ANSI_COLOR_CYAN "   IPv6" ANSI_COLOR_RESET ":\t" RGB_COLOR_BRIGHT_YELLOW "Not Set" ANSI_COLOR_RESET);
                }

                if (PETAL->MAC[0] != 0x00) {
                        kui_add_line(ANSI_COLOR_CYAN "    MAC" ANSI_COLOR_RESET ":\t%s", PETAL->MAC);
                } else {
                        kui_add_line(ANSI_COLOR_CYAN "    MAC" ANSI_COLOR_RESET ":\t" RGB_COLOR_BRIGHT_YELLOW "Not Set" ANSI_COLOR_RESET);
                }

                if (PETAL->NOTE[0] != 0x00) {
                        kui_add_line(ANSI_COLOR_CYAN "  NOTES" ANSI_COLOR_RESET ":\t%s", PETAL->NOTE);
                } else {
                        kui_add_line(ANSI_COLOR_CYAN "  NOTES" ANSI_COLOR_RESET ":\t" RGB_COLOR_BRIGHT_YELLOW "None" ANSI_COLOR_RESET);
                }
        }

        if (
                kscan_data_valid(PETAL) == ISFALSE
                || PETAL->SCAN.LAST_SCAN_NS == 0
        ) {
                if (OBSERVED_ONLY != ISTRUE) {
                        if (DISPLAY_TID == ISTRUE) {
                                kui_add_line(ANSI_COLOR_CYAN "   SCAN" ANSI_COLOR_RESET ":\t" RGB_COLOR_BRIGHT_YELLOW "NEVER" ANSI_COLOR_RESET);
                        }
                        kui_add_line(ANSI_COLOR_CYAN " ICMPv4" ANSI_COLOR_RESET ":\t" RGB_COLOR_BRIGHT_YELLOW "NEVER" ANSI_COLOR_RESET);
                        kui_add_line(ANSI_COLOR_CYAN " ICMPv6" ANSI_COLOR_RESET ":\t" RGB_COLOR_BRIGHT_YELLOW "NEVER" ANSI_COLOR_RESET);
                }
                return;
        }

        memset(SCAN_TIME, 0x00, sizeof(SCAN_TIME));
        kscan_format_ns(PETAL->SCAN.LAST_SCAN_NS, SCAN_TIME, sizeof(SCAN_TIME));
        if (DISPLAY_TID == ISTRUE) {
                kui_add_line(
                        ANSI_COLOR_CYAN "   SCAN" ANSI_COLOR_RESET ":\t"
                        RGB_COLOR_BRIGHT_GREEN "%s" ANSI_COLOR_RESET,
                        SCAN_TIME
                );
        }

        if (
                (PETAL->SCAN.ICMPV4 & SCAN_RESULT_RAN)
                && (
                        OBSERVED_ONLY != ISTRUE
                        || (PETAL->SCAN.ICMPV4 & (SCAN_RESULT_REPLY | SCAN_RESULT_NEIGHBOR))
                )
        ) {
                memset(ICMP_TIME, 0x00, sizeof(ICMP_TIME));
                kscan_format_ns(PETAL->SCAN.ICMPV4_SCAN_NS, ICMP_TIME, sizeof(ICMP_TIME));
                kui_add_line(
                        ANSI_COLOR_CYAN " ICMPv4" ANSI_COLOR_RESET ":\t"
                        RGB_COLOR_BRIGHT_GREEN "%s" ANSI_COLOR_RESET
                        "  %s%s" ANSI_COLOR_RESET,
                        ICMP_TIME,
                        targets_display_icmpv4_color(PETAL->SCAN.ICMPV4),
                        kscan_icmpv4_result(PETAL->SCAN.ICMPV4)
                );
        } else if (OBSERVED_ONLY != ISTRUE) {
                kui_add_line(
                        ANSI_COLOR_CYAN " ICMPv4" ANSI_COLOR_RESET ":\t"
                        RGB_COLOR_BRIGHT_YELLOW "NEVER" ANSI_COLOR_RESET
                );
        }

        ICMPV6_STATE = kscan_icmpv6_state(PETAL);
        ICMPV6_SCAN_NS = kscan_icmpv6_scan_ns(PETAL);

        if (
                (ICMPV6_STATE & SCAN_RESULT_RAN)
                && ICMPV6_SCAN_NS != 0
                && (
                        OBSERVED_ONLY != ISTRUE
                        || (ICMPV6_STATE & (SCAN_RESULT_REPLY | SCAN_RESULT_NEIGHBOR))
                )
        ) {
                memset(ICMP_TIME, 0x00, sizeof(ICMP_TIME));
                kscan_format_ns(ICMPV6_SCAN_NS, ICMP_TIME, sizeof(ICMP_TIME));
                kui_add_line(
                        ANSI_COLOR_CYAN " ICMPv6" ANSI_COLOR_RESET ":\t"
                        RGB_COLOR_BRIGHT_GREEN "%s" ANSI_COLOR_RESET
                        "  %s%s" ANSI_COLOR_RESET,
                        ICMP_TIME,
                        targets_display_icmpv4_color(ICMPV6_STATE),
                        kscan_icmpv6_result(ICMPV6_STATE)
                );
        } else if (OBSERVED_ONLY != ISTRUE) {
                kui_add_line(
                        ANSI_COLOR_CYAN " ICMPv6" ANSI_COLOR_RESET ":\t"
                        RGB_COLOR_BRIGHT_YELLOW "NEVER" ANSI_COLOR_RESET
                );
        }
}
