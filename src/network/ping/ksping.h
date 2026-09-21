// Copyright 2026 Jamison A. Drapeau
#ifndef __KSPING__H
#define __KSPING__H
#include "data.h"
#include <sys/socket.h>
#include <netinet/in.h>
void ksping_single(_carry_forward * _prog_data);
void ksping_multi(_carry_forward * _prog_data);
void ksping6(_carry_forward * _prog_data);
#endif
