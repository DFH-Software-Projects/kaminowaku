// Copyright 2026 Jamison A. Drapeau
// Internal bounded dispatcher for authorized project scan work.
#ifndef __KSCAN_DISPATCH__H
#define __KSCAN_DISPATCH__H
#include "data.h"
#include <stddef.h>
typedef struct KSCAN_SESSION KSCAN_SESSION;
typedef struct KSCAN_TRANSACTION KSCAN_TRANSACTION;
typedef struct KSCAN_EVENT KSCAN_EVENT;
typedef enum {
        KSCAN_DISPATCH_OK = 0,
        KSCAN_DISPATCH_DEFER = 1,
        KSCAN_DISPATCH_ERROR = -1
} kscan_dispatch_status_t;
int kscan_dispatcher_open(KSCAN_SESSION *SESSION);
void kscan_dispatcher_close(KSCAN_SESSION *SESSION);
int kscan_dispatch_build(KSCAN_SESSION *SESSION, const unsigned char *TID);
int kscan_dispatch_ready(KSCAN_SESSION *SESSION, KSCAN_TRANSACTION *TRANSACTION);
KSCAN_TRANSACTION *kscan_dispatch_ready_pop(KSCAN_SESSION *SESSION);
size_t kscan_dispatch_ready_count(KSCAN_SESSION *SESSION);
int kscan_dispatch_event(KSCAN_SESSION *SESSION, KSCAN_EVENT *EVENT);
KSCAN_EVENT *kscan_dispatch_event_pop(KSCAN_SESSION *SESSION);
size_t kscan_dispatch_event_count(KSCAN_SESSION *SESSION);
#endif
