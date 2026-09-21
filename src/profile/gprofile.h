// Copyright 2026 Jamison A. Drapeau
#ifndef __GPROFILE__H
#define __GPROFILE__H
#include "data.h"
#define GPROFILE_SAVE_EXISTS 1
const char * gprofile_last_error(void);
int load_global_profile(const unsigned char * PROFILE_NAME, _carry_forward * _prog_data);
int gprofile_set_runtime_value(const unsigned char * KEY, const unsigned char * VALUE, _carry_forward * _prog_data);
int gprofile_save_runtime(const unsigned char * PROFILE_NAME, int8_t ALLOW_OVERWRITE, _carry_forward * _prog_data);
#endif
