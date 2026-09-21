// Copyright 2026 Jamison A. Drapeau
#include "projects.h"
#include "helpers.h"
#include "kui.h"
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <arpa/inet.h>

// @@ Translate the active KAMI profile into a NOSIX runtime configuration
static int projects_nosix_config(_carry_forward * _prog_data, nosix_config_t * CONFIG) {
        memset(CONFIG, 0x00, sizeof(*CONFIG));

        snprintf(
                CONFIG->tx_interface,
                sizeof(CONFIG->tx_interface),
                "%s",
                (char*)_prog_data->gprof.tx_interface
        );

        snprintf(
                CONFIG->rx_interface,
                sizeof(CONFIG->rx_interface),
                "%s",
                (char*)_prog_data->gprof.rx_interface
        );

        if (_prog_data->gprof.tx_source_ip[0] == 0x00) {
                CONFIG->flags |= NOSIX_CONFIG_TX_SOURCE_AUTO;
        } else if (
                inet_pton(
                        AF_INET,
                        (char*)_prog_data->gprof.tx_source_ip,
                        CONFIG->tx_source_address.bytes.ipv4
                ) == 1
        ) {
                CONFIG->tx_source_address.family = NOSIX_ADDRESS_IPV4;
        } else if (
                inet_pton(
                        AF_INET6,
                        (char*)_prog_data->gprof.tx_source_ip,
                        CONFIG->tx_source_address.bytes.ipv6
                ) == 1
        ) {
                CONFIG->tx_source_address.family = NOSIX_ADDRESS_IPV6;
        } else {
                kui_add_line(
                        NOTICE_ERROR
                        "Invalid source address in global profile."
                );
                return ABNORMAL;
        }

        if (_prog_data->gprof.tx_gateway[0] == 0x00) {
                CONFIG->flags |= NOSIX_CONFIG_TX_GATEWAY_AUTO;
        } else if (
                inet_pton(
                        AF_INET,
                        (char*)_prog_data->gprof.tx_gateway,
                        CONFIG->tx_gateway_address.bytes.ipv4
                ) == 1
        ) {
                CONFIG->tx_gateway_address.family = NOSIX_ADDRESS_IPV4;
        } else if (
                inet_pton(
                        AF_INET6,
                        (char*)_prog_data->gprof.tx_gateway,
                        CONFIG->tx_gateway_address.bytes.ipv6
                ) == 1
        ) {
                CONFIG->tx_gateway_address.family = NOSIX_ADDRESS_IPV6;
        } else {
                kui_add_line(
                        NOTICE_ERROR
                        "Invalid gateway address in global profile."
                );
                return ABNORMAL;
        }

        if (_prog_data->gprof.tx_strict == ISTRUE) {
                CONFIG->flags |= NOSIX_CONFIG_TX_STRICT;
        }

        if (_prog_data->gprof.rx_promiscuous == ISTRUE) {
                CONFIG->flags |= NOSIX_CONFIG_RX_PROMISCUOUS;
        }

        switch (_prog_data->gprof.tx_checksum_mode) {
                case CHECKSUM_AUTO:
                        CONFIG->checksum_mode = NOSIX_CHECKSUM_AUTO;
                        break;
                case CHECKSUM_MANUAL:
                        CONFIG->checksum_mode = NOSIX_CHECKSUM_MANUAL;
                        break;
                default:
                        kui_add_line(
                                NOTICE_ERROR
                                "Invalid checksum policy."
                        );
                        return ABNORMAL;
        }

        switch (_prog_data->gprof.tx_ipv4_id_generation) {
                case TXG_RANDOM:
                        CONFIG->ipv4_id_mode = NOSIX_IPV4_ID_RANDOM;
                        break;
                case TXG_STATIC:
                        CONFIG->ipv4_id_mode = NOSIX_IPV4_ID_STATIC;
                        break;
                case TXG_INCREMENT:
                        CONFIG->ipv4_id_mode = NOSIX_IPV4_ID_INCREMENT;
                        break;
                default:
                        kui_add_line(
                                NOTICE_ERROR
                                "Invalid IPv4 ID generation policy."
                        );
                        return ABNORMAL;
        }

        CONFIG->ipv4_id_value = 0x0000;
        CONFIG->ipv4_ttl = (uint8_t)_prog_data->gprof.tx_ipv4_ttl;
        CONFIG->ipv6_hop_limit = (uint8_t)_prog_data->gprof.tx_ipv6_hop_limit;
        CONFIG->rx_snaplength = (uint32_t)_prog_data->gprof.rx_snaplength;
        CONFIG->rx_timeout_ms = (int32_t)_prog_data->gprof.rx_timeout_ms;

        return NORMAL;
}

// @@ Open NOSIX for an active project
static nosix_status_t projects_nosix_open(_carry_forward * _prog_data) {
        nosix_config_t CONFIG;

        if (!_prog_data) {
                return NOSIX_ERR_ARGUMENT;
        }

        if (projects_nosix_config(_prog_data, &CONFIG) != NORMAL) {
                _prog_data->nosix_status = NOSIX_ERR_ARGUMENT;
                return _prog_data->nosix_status;
        }

        _prog_data->nosix_status = nosix_init(
                &_prog_data->nosix_net,
                &CONFIG
        );

        return _prog_data->nosix_status;
}

// @@ Reconfigure the active project network runtime after a profile update
nosix_status_t projects_nosix_reopen(_carry_forward * _prog_data) {
        nosix_config_t CONFIG;

        if (!_prog_data) {
                return NOSIX_ERR_ARGUMENT;
        }

        if (_prog_data->active_project[0] == 0x00) {
                _prog_data->nosix_status = NOSIX_ERR_STATE;
                return _prog_data->nosix_status;
        }

        if (projects_nosix_config(_prog_data, &CONFIG) != NORMAL) {
                _prog_data->nosix_status = NOSIX_ERR_ARGUMENT;
                return _prog_data->nosix_status;
        }

        _prog_data->nosix_status = nosix_reopen(
                &_prog_data->nosix_net,
                &CONFIG
        );

        return _prog_data->nosix_status;
}

// @@ Report project-bound NOSIX startup state
static void projects_nosix_open_notice(_carry_forward * _prog_data) {
        if (projects_nosix_open(_prog_data) == NOSIX_OK) {
                kui_add_line(
                        NOTICE_SUCCESS
                        "NOSIX network runtime online."
                );
        } else {
                kui_add_line(
                        NOTICE_ERROR
                        "NOSIX network runtime failed to initialize (%d).",
                        _prog_data->nosix_status
                );
        }
}

// @@ Try to create a new project
void projects_try_new(const unsigned char *_PROJECT, _carry_forward * _prog_data) {
        char pathing_buffer[MAX_PATH];
        memset(pathing_buffer, 0x00, MAX_PATH);
        strcats(pathing_buffer, MAX_PATH, KAMI_USER_PROJECTS_DIR);
        strcats(pathing_buffer, MAX_PATH, _PROJECT);
        struct stat info;
        if (stat(pathing_buffer, &info) != MATCH) {
                if (mkdir(pathing_buffer, 0755) == NORMAL) {
                        // Change active project and inform success
                        kui_add_line(
                               NOTICE_SUCCESS "Project" 
                               ANSI_COLOR_CYAN " %s " ANSI_COLOR_RESET
                               "successfully created."
                               , (char*)_PROJECT
                        );
                        kui_add_line(
                                NOTICE_INFO "Use the 'targets' command to start building out your project."
                        );
                        memset(_prog_data->active_project, 0x00, MAX_BLOCK);
                        strcats((char*)_prog_data->active_project, sizeof(_prog_data->active_project), (char*)_PROJECT);
                        _prog_data->active_project_has_targets = ISFALSE;
                        _prog_data->active_project_has_been_scanned = ISFALSE;
                        _prog_data->active_project_last_scan_ns = RESET;
                        projects_nosix_open_notice(_prog_data);
                        _prog_data->f_type = S_DEFAULT;
                } else {
                        kui_add_line(
                                NOTICE_ERROR "Failure to create directory."
                        );
                        _prog_data->f_type = S_DEFAULT;
                }
        } else if (S_ISDIR(info.st_mode)) {
                kui_add_line(
                        NOTICE_WARNING "Project"
                        ANSI_COLOR_CYAN " %s " ANSI_COLOR_RESET
                        "already exists."
                        , (char*)_PROJECT
                );
                _prog_data->f_type = S_DEFAULT;
        } else {
                kui_add_line(
                        NOTICE_WARNING "Exists but is a file."
                );
                kui_add_line("< Choose another name.");
                _prog_data->f_type = S_DEFAULT;
        }
        return;
}

// @@ Try to load an existing project
void projects_try_load(const unsigned char *_PROJECT, _carry_forward * _prog_data) {
        // Fail state tracking
        int _FAILURE = ISFALSE;
        // Path Buffer
        char pathing_buffer[MAX_PATH];
        memset(pathing_buffer, 0x00, MAX_PATH);
        strcats(pathing_buffer, MAX_PATH, KAMI_USER_PROJECTS_DIR);
        strcats(pathing_buffer, MAX_PATH, _PROJECT);
        // Iterate list and load
        struct stat info;
        if (stat(pathing_buffer, &info) != MATCH) {
                kui_add_line(
                       NOTICE_WARNING "Project"
                        RGB_COLOR_BRIGHT_YELLOW " %s " ANSI_COLOR_RESET
                        "not found."
                );
                kui_add_line(
                        NOTICE_INFO "Usage: load [" 
                        ANSI_COLOR_CYAN "<Project_Name>" ANSI_COLOR_RESET 
                        "] : Running without arguments displays list of projects."
                );
                _prog_data->f_type = S_DEFAULT;
        } else if (!S_ISDIR(info.st_mode)) {
                kui_add_line(
                        NOTICE_WARNING "Project "
                        RGB_COLOR_BRIGHT_YELLOW "%s" ANSI_COLOR_RESET
                        " is not a directory.",
                        (char*)_PROJECT
                );
                _prog_data->f_type = S_DEFAULT;
        } else {
                DIR * targets = opendir(pathing_buffer);
                struct dirent * entries;

                if (!targets) {
                        kui_add_line(
                                NOTICE_ERROR "Unable to open project "
                                ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET ".",
                                (char*)_PROJECT
                        );
                        _prog_data->f_type = S_DEFAULT;
                        return;
                }

                _prog_data->active_project_has_targets = ISFALSE;
                _prog_data->active_project_has_been_scanned = ISFALSE;
                _prog_data->active_project_last_scan_ns = RESET;
                memset(_prog_data->active_project, 0x00, MAX_BLOCK);
                strcats((char*)_prog_data->active_project, sizeof(_prog_data->active_project), (char*)_PROJECT);
                struct stat tid_dir_info;
                struct stat tid_datafile_info;
                FLOWER * CURRENT_FLOWER_NODE_INDEX = _prog_data->active_project_flower;
                unsigned char TID_DIR_BUF[MAX_BLOCK]; memset(TID_DIR_BUF, 0x00, MAX_BLOCK);
                unsigned char TID_DATAFILE_BUF[MAX_BLOCK]; memset(TID_DATAFILE_BUF, 0x00, MAX_BLOCK);
                while ((entries = readdir(targets)) != NULL) {
                        if ((strcmp(entries->d_name, ".") == MATCH) || (strcmp(entries->d_name, "..") == MATCH)) {
                                continue;
                        }
                        if (vtid(entries->d_name) == ABNORMAL) {
                                continue;
                        }
                        snprintf(TID_DIR_BUF, sizeof(TID_DIR_BUF), "%s/%s", pathing_buffer, entries->d_name);
                        if (stat(TID_DIR_BUF, &tid_dir_info) == NORMAL) {
                                if (!S_ISDIR(tid_dir_info.st_mode) ) {
                                        continue;
                                }

                        }
                        snprintf(TID_DATAFILE_BUF, sizeof(TID_DATAFILE_BUF), "%s/%s/.data", pathing_buffer, entries->d_name);
                        int TEST_FD = open(TID_DATAFILE_BUF, O_RDONLY);
                        if (TEST_FD < 0) {
                                continue;
                        }
                        TARGET PETAL;
                        ssize_t R = pread(TEST_FD, &PETAL, sizeof(PETAL), 0);
                        if (R != (ssize_t)sizeof(PETAL)) {
                                close(TEST_FD);
                                continue;
                        } else {
                                close(TEST_FD);
                        }

                        // @@ Recover the newest scan timestamp from persisted target data
                        if (
                                PETAL.SCAN.SCAN_VERSION == TARGET_SCAN_DATA_VERSION
                                &&
                                PETAL.SCAN.LAST_SCAN_NS > _prog_data->active_project_last_scan_ns
                        ) {
                                _prog_data->active_project_last_scan_ns = PETAL.SCAN.LAST_SCAN_NS;
                                _prog_data->active_project_has_been_scanned = ISTRUE;
                        }

                        memset(TID_DIR_BUF, 0x00, MAX_BLOCK);
                        FLOWER *new_FLOWER_NODE = malloc(sizeof(FLOWER));
                        if (!new_FLOWER_NODE) {
                                _FAILURE = ISTRUE;
                                continue;
                        }

                        ++_prog_data->active_project_target_count;
                        if (_prog_data->debug_flag == ISTRUE) {
                                kui_add_line_and_render(
                                        "!["
                                        ANSI_COLOR_MAGENTA
                                        "projects_try_load()"
                                        ANSI_COLOR_RESET
                                        "]> "
                                        ANSI_COLOR_CYAN
                                        "%d "
                                        ANSI_COLOR_RESET
                                        "--> "
                                        ANSI_COLOR_CYAN
                                        "%s"
                                        ANSI_COLOR_RESET
                                        , _prog_data->active_project_target_count
                                        , entries->d_name
                                );
                        }
                        memcpy(new_FLOWER_NODE->TID, entries->d_name, TID_BLOCK);
                        new_FLOWER_NODE->FD = FD_RESET;
                        new_FLOWER_NODE->LAST_MAP = RESET;
                        new_FLOWER_NODE->MAP_SIZE = RESET;
                        new_FLOWER_NODE->MAP_ADDRESS = NULL;
                        new_FLOWER_NODE->PETAL = NULL;
                        new_FLOWER_NODE->NEXT = NULL;
                        CURRENT_FLOWER_NODE_INDEX->NEXT = new_FLOWER_NODE;
                        CURRENT_FLOWER_NODE_INDEX = new_FLOWER_NODE;
                        if (_prog_data->debug_flag == ISTRUE) {
                                kui_add_line_and_render(
                                        "!["
                                        ANSI_COLOR_MAGENTA
                                        "projects_try_load()"
                                        ANSI_COLOR_RESET
                                        "]> NODE "
                                        ANSI_COLOR_CYAN
                                        "%s"
                                        ANSI_COLOR_RESET
                                        " @ "
                                        ANSI_COLOR_YELLOW
                                        "%p"
                                        ANSI_COLOR_RESET
                                        , new_FLOWER_NODE->TID
                                        , new_FLOWER_NODE
                                );
                        }
                        kui_add_line(
                                NOTICE_SUCCESS "Found target"
                                ANSI_COLOR_CYAN " %s " ANSI_COLOR_RESET
                                , new_FLOWER_NODE->TID
                        );
                        _prog_data->active_project_has_targets = ISTRUE;
                }
                closedir(targets);
                if (_FAILURE == ISFALSE) {
                        kui_add_line(
                                NOTICE_SUCCESS "Project"
                                ANSI_COLOR_CYAN " %s " ANSI_COLOR_RESET
                                "successfully loaded."
                                , (char*)_PROJECT
                        );
                } else {
                        kui_add_line(
                                NOTICE_WARNING "Project"
                                RGB_COLOR_BRIGHT_YELLOW " %s " ANSI_COLOR_RESET
                                "could not be fully loaded."
                                , (char*)_PROJECT
                        );
                }
                projects_nosix_open_notice(_prog_data);
                _prog_data->f_type = S_DEFAULT;
        }
        return;
}

// @@ Unload Active Project
void projects_try_unload(_carry_forward * _prog_data) {
        _prog_data->nosix_status = nosix_close(&_prog_data->nosix_net);

        if (_prog_data->nosix_status == NOSIX_OK) {
                kui_add_line(
                        NOTICE_SUCCESS
                        "NOSIX network runtime offline."
                );
        } else {
                kui_add_line(
                        NOTICE_ERROR
                        "NOSIX network runtime failed to close (%d).",
                        _prog_data->nosix_status
                );
        }

        memset(_prog_data->active_project, 0x00, MAX_BLOCK);
        _prog_data->active_project_target_count = RESET;
        _prog_data->active_project_has_been_scanned = ISFALSE;
        _prog_data->active_project_last_scan_ns = RESET;
        kui_add_line(NOTICE_SUCCESS "Unloaded active project.");
        return;
}
