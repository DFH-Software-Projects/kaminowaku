// Copyright 2026 Jamison A. Drapeau
#ifndef __PROFILE__H
#define __PROFILE__H
#include "data.h"
void profile_display_global(_carry_forward * _prog_data);
void profile_print_usage(void);
void profile_print_load_usage(void);
void profile_list_available(void);
void profile_try_load(const unsigned char * PROFILE_NAME, _carry_forward * _prog_data);
void profile_try_set(const unsigned char * KEY, const unsigned char * VALUE, _carry_forward * _prog_data);
void profile_try_save(const unsigned char * PROFILE_NAME, int8_t ALLOW_OVERWRITE, _carry_forward * _prog_data);
#endif
