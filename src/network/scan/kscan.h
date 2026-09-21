// Copyright 2026 Jamison A. Drapeau
#ifndef __KSCAN__H
#define __KSCAN__H
#include "data.h"
#include <stddef.h>
#include <stdint.h>
typedef struct KSCAN_SESSION KSCAN_SESSION;
uint64_t kscan_now_ns(void);
void kscan_format_ns(uint64_t TIMESTAMP_NS, char * BUFFER, size_t BUFFER_SIZE);
int8_t kscan_data_valid(const TARGET * PETAL);
const char * kscan_icmpv4_result(uint8_t RESULT);
int kscan_record_icmpv4(_carry_forward * _prog_data, uint8_t RESULT);
uint64_t kscan_icmpv6_scan_ns(const TARGET * PETAL);
uint8_t kscan_icmpv6_state(const TARGET * PETAL);
const char * kscan_icmpv6_result(uint8_t RESULT);
int kscan_record_icmpv6(_carry_forward * _prog_data, uint8_t RESULT);
int kscan_session_open(_carry_forward * _prog_data, KSCAN_SESSION ** SESSION);
int kscan_session_schedule(KSCAN_SESSION * SESSION);
int8_t kscan_session_complete(KSCAN_SESSION * SESSION);
void kscan_session_cancel(KSCAN_SESSION * SESSION);
void kscan_session_close(KSCAN_SESSION ** SESSION);
int kscan_session_run_tx(_carry_forward * _prog_data);
#endif
