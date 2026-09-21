// Copyright 2026 Jamison A. Drapeau
// This code was heavily generated with the help of ChatGPT
#ifndef __KIO__H
#define __KIO__H
#include "data.h"
#include <stddef.h>
void enable_raw_mode(void);
void disable_raw_mode(void);
int read_line(_carry_forward * _prog_data);
#endif
