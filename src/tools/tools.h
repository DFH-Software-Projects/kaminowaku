// Copyright 2026 Jamison A. Drapeau
#ifndef __TOOLS__H
#define __TOOLS__H

#include "data.h"

// @@ External tool registry helpers
void tools_print_usage(void);
void tools_add(_carry_forward * _prog_data);
void tools_add_context(_carry_forward * _prog_data);
void tools_list(void);
void tools_del(_carry_forward * _prog_data);
void tools_del_context(_carry_forward * _prog_data);

#endif
