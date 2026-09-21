// Copyright 2026 Jamison A. Drapeau
#ifndef __PROJECTS__H
#define __PROJECTS__H
#include "data.h"
void projects_try_new(const unsigned char *_PROJECT, _carry_forward * _prog_data);
void projects_try_load(const unsigned char *_PROJECT, _carry_forward * _prog_data);
void projects_try_unload(_carry_forward * _prog_data);
nosix_status_t projects_nosix_reopen(_carry_forward * _prog_data);
#endif
