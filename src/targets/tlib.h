// Copyright 2026 Jamison A. Drapeau
#ifndef __T_LIB__H
#define __T_LIB__H
#include "data.h"
// Define preservation param for readability
void targets_free_nodes(FLOWER *ANCHOR);
int build_petal_path(char *out, c_size_t outsz, const unsigned char *wd, const unsigned char *project, const unsigned char *tid);
void targets_buffer_path_stack_unsafe(_carry_forward * _prog_data, int SIZE, const char * TID, char * PATH_BUFFER);
int targets_write_petal_data(const char * SAVE_DATA, const TARGET * TARGET_DATA);
int targets_read_petal_data(const char * SAVE_DATA, TARGET * FILL_TARGET_DATA);
void targets_display_petal(
        const unsigned char * TID,
        const TARGET * PETAL,
        int8_t DISPLAY_TID,
        int8_t OBSERVED_ONLY
);
int targets_modify_petal_url(_carry_forward * _prog_data, TARGET * MODIFY_PETAL, unsigned char * ADD_VALUE);
int targets_modify_petal_ipv4(_carry_forward * _prog_data, TARGET * MODIFY_PETAL, unsigned char * ADD_VALUE);
int targets_modify_petal_ipv6(_carry_forward * _prog_data, TARGET * MODIFY_PETAL, unsigned char * ADD_VALUE);
int targets_modify_petal_mac(_carry_forward * _prog_data, TARGET * MODIFY_PETAL, unsigned char * ADD_VALUE);
int targets_modify_petal_note(_carry_forward * _prog_data, TARGET * MODIFY_PETAL, unsigned char * ADD_VALUE);
int targets_delete_by_tid(_carry_forward * _prog_data, const unsigned char * TID);
void targets_load_petal_data(const char * SAVE_DATA, FLOWER * ENTRY_POINT);
TARGET * targets_buffer_target(void);
FLOWER * flower_create_node(FLOWER *ANCHOR_NODE, char * PASS_IN_TID);
FLOWER * targets_find_by_tid_return_flower_node(_carry_forward * _prog_data, const unsigned char *TID, FLOWER **PREV_OUT);
#endif