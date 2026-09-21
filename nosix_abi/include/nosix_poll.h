// Copyright 2026 Jamison A. Drapeau
#ifndef NOSIX_POLL_H
#define NOSIX_POLL_H
#include "nosix.h"
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
nosix_status_t nosix_read_timeout(nosix_t *net, nosix_capture_t *capture, int32_t timeout_ms);
nosix_status_t nosix_lock_auto_interface(nosix_t *net);
nosix_status_t nosix_restore_auto_interface(nosix_t *net);
#ifdef __cplusplus
}
#endif
#endif
