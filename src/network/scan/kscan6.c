// Copyright 2026 Jamison A. Drapeau
#include "kscan.h"
#include "tlib.h"

#include <string.h>

// @@ ICMPv6 scan extension lives inside the existing reserved scan bytes.
// Keep TARGET_SCAN_DATA at LAST_BLOCK bytes and preserve the current on-disk ABI.
#define KSCAN_ICMPV6_SCAN_NS_OFFSET 0U
#define KSCAN_ICMPV6_RESULT_OFFSET  8U
#define KSCAN_ICMPV6_RESERVED_USED  9U

_Static_assert(
        sizeof(((TARGET_SCAN_DATA *)0)->RESERVED) >= KSCAN_ICMPV6_RESERVED_USED,
        "TARGET scan metadata does not have enough reserved space for ICMPv6"
);

static uint64_t kscan_icmpv6_read_ns(const TARGET * PETAL) {
        uint64_t VALUE = 0;

        if (!PETAL) {
                return 0;
        }

        memcpy(
                &VALUE,
                PETAL->SCAN.RESERVED + KSCAN_ICMPV6_SCAN_NS_OFFSET,
                sizeof(VALUE)
        );

        return VALUE;
}

static uint8_t kscan_icmpv6_read_result(const TARGET * PETAL) {
        if (!PETAL) {
                return 0;
        }

        return PETAL->SCAN.RESERVED[KSCAN_ICMPV6_RESULT_OFFSET];
}

uint64_t kscan_icmpv6_scan_ns(const TARGET * PETAL) {
        if (
                !PETAL
                || PETAL->SCAN.SCAN_VERSION != TARGET_SCAN_DATA_VERSION
        ) {
                return 0;
        }

        return kscan_icmpv6_read_ns(PETAL);
}

uint8_t kscan_icmpv6_state(const TARGET * PETAL) {
        if (
                !PETAL
                || PETAL->SCAN.SCAN_VERSION != TARGET_SCAN_DATA_VERSION
        ) {
                return 0;
        }

        return kscan_icmpv6_read_result(PETAL);
}

const char * kscan_icmpv6_result(uint8_t RESULT) {
        if (!(RESULT & SCAN_RESULT_RAN)) {
                return "NEVER";
        }

        if (RESULT & SCAN_RESULT_REPLY) {
                return "REPLY";
        }

        if (RESULT & SCAN_RESULT_TIMEOUT) {
                return "TIMEOUT";
        }

        if (RESULT & SCAN_RESULT_ROUTE) {
                return "ROUTE";
        }

        if (RESULT & SCAN_RESULT_NEIGHBOR) {
                return "NEIGHBOR";
        }

        if (RESULT & SCAN_RESULT_ERROR) {
                return "ERROR";
        }

        return "UNKNOWN";
}

int kscan_record_icmpv6(
        _carry_forward * _prog_data,
        uint8_t RESULT
) {
        uint64_t SCAN_NS;
        uint8_t STORED_RESULT;
        char PETAL_PATH[MAX_PATH];
        TARGET_SCAN_DATA PREVIOUS_SCAN;
        TARGET * PETAL;
        int WRITE_STATUS;

        if (
                !_prog_data
                || !_prog_data->active_project_active_target
                || !_prog_data->active_project_active_target->PETAL
        ) {
                return ABNORMAL;
        }

        PETAL = _prog_data->active_project_active_target->PETAL;
        PREVIOUS_SCAN = PETAL->SCAN;

        SCAN_NS = kscan_now_ns();
        if (SCAN_NS == 0) {
                return ABNORMAL;
        }

        if (
                PETAL->SCAN.SCAN_VERSION
                != TARGET_SCAN_DATA_VERSION
        ) {
                memset(
                        &PETAL->SCAN,
                        0x00,
                        sizeof(PETAL->SCAN)
                );

                PETAL->SCAN.SCAN_VERSION = TARGET_SCAN_DATA_VERSION;
        }

        STORED_RESULT = (uint8_t)(
                SCAN_RESULT_RAN
                |
                RESULT
        );

        memcpy(
                PETAL->SCAN.RESERVED
                        + KSCAN_ICMPV6_SCAN_NS_OFFSET,
                &SCAN_NS,
                sizeof(SCAN_NS)
        );

        PETAL->SCAN.RESERVED[
                KSCAN_ICMPV6_RESULT_OFFSET
        ] = STORED_RESULT;

        PETAL->SCAN.LAST_SCAN_NS = SCAN_NS;

        memset(PETAL_PATH, 0x00, sizeof(PETAL_PATH));

        if (
                build_petal_path(
                        PETAL_PATH,
                        sizeof(PETAL_PATH),
                        _prog_data->wd,
                        _prog_data->active_project,
                        _prog_data->active_project_active_target->TID
                ) != NORMAL
        ) {
                PETAL->SCAN = PREVIOUS_SCAN;
                return ABNORMAL;
        }

        WRITE_STATUS = targets_write_petal_data(
                PETAL_PATH,
                PETAL
        );

        if (WRITE_STATUS != NORMAL) {
                PETAL->SCAN = PREVIOUS_SCAN;
                return ABNORMAL;
        }

        _prog_data->active_project_has_been_scanned = ISTRUE;

        if (SCAN_NS > _prog_data->active_project_last_scan_ns) {
                _prog_data->active_project_last_scan_ns = SCAN_NS;
        }

        return NORMAL;
}
