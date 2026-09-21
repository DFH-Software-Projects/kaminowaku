// Copyright 2026 Jamison A. Drapeau
#ifndef __TARGETS__H
#define __TARGETS__H
#include "data.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
void targets_free_nodes(FLOWER *ANCHOR);
void targets_print_usage(void);
void targets_print_usage_guard(void);
void targets_add_target(_carry_forward * _prog_data);
void targets_del_target(_carry_forward * _prog_data, int8_t MODE);
void targets_del_unobserved(_carry_forward * _prog_data);
void targets_display_from_project(_carry_forward * _prog_data);
void targets_display_from_project_guard(_carry_forward * _prog_data);
void targets_display_from_context(_carry_forward * _prog_data);
void targets_context(_carry_forward * _prog_data, int8_t MODE);
void targets_set_identifier(_carry_forward * _prog_data);
#if defined(CMD_SCAN_H_)
#define targets_print_usage targets_print_usage_guard
#define targets_display_from_project targets_display_from_project_guard
#endif
#endif
