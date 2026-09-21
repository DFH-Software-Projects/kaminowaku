// Copyright 2026 Jamison A. Drapeau
#ifndef __HELPERS__H
#define __HELPERS__H
#include "data.h"
void strcats(char * dst, c_size_t dsize, const char * src, ...);
int vtid(const char *TID);
char * timestamp(void);
char * file_timestamp_ns(void);
int validate_regular_file_presence(const char * PATH);
// SRC and DST must be regular paths; symbolic links are rejected and DST must not exist.
int copy_regular_file(const char * SRC, const char * DST);
#endif
