// Copyright 2026 Jamison A. Drapeau
#ifndef __KWIRE__H
#define __KWIRE__H
#include "data.h"
#include "kscan.h"
#include "kui.h"
#include <fcntl.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

nosix_status_t nosix_read_timeout(
        nosix_t *net,
        nosix_capture_t *capture,
        int32_t timeout_ms
);

nosix_status_t nosix_lock_auto_interface(
        nosix_t *net
);

nosix_status_t nosix_restore_auto_interface(
        nosix_t *net
);

#define KPCAP_MAGIC_NS 0xA1B23C4DU
#define KPCAP_LINKTYPE_ETHERNET 1U
#define KPCAP_LINKTYPE_RAW_IPV4 101U

typedef struct KPCAP_GLOBAL_HEADER {
        uint32_t MAGIC;
        uint16_t VERSION_MAJOR;
        uint16_t VERSION_MINOR;
        int32_t  THIS_ZONE;
        uint32_t SIGFIGS;
        uint32_t SNAPLEN;
        uint32_t NETWORK;
} KPCAP_GLOBAL_HEADER;

typedef struct KPCAP_PACKET_HEADER {
        uint32_t TIMESTAMP_SECONDS;
        uint32_t TIMESTAMP_NANOSECONDS;
        uint32_t CAPTURE_LENGTH;
        uint32_t WIRE_LENGTH;
} KPCAP_PACKET_HEADER;

typedef struct KPCAP {
        FILE * FILE_HANDLE;
        uint64_t SCAN_TIMESTAMP_NS;
        uint64_t PACKET_COUNT;
        uint32_t NETWORK;
        char PATH[MAX_PATH];
} KPCAP;

static KPCAP * KWIRE_ACTIVE_PCAP = NULL;

static inline int kwire_pcap_open(
        _carry_forward * _prog_data,
        KPCAP * PCAP,
        const char * SCAN_TYPE,
        const unsigned char * TID,
        uint64_t SCAN_TIMESTAMP_NS
) {
        KPCAP_GLOBAL_HEADER HEADER;
        int FD;

        if (
                !_prog_data
                || !PCAP
                || !SCAN_TYPE
                || !TID
                || SCAN_TIMESTAMP_NS == 0
        ) {
                return ABNORMAL;
        }

        memset(PCAP, 0x00, sizeof(*PCAP));
        memset(&HEADER, 0x00, sizeof(HEADER));

        snprintf(
                PCAP->PATH,
                sizeof(PCAP->PATH),
                "%s/%s%s/%s/%s-%s-%" PRIu64 ".pcap",
                _prog_data->wd,
                KAMI_USER_PROJECTS_DIR,
                _prog_data->active_project,
                TID,
                SCAN_TYPE,
                TID,
                SCAN_TIMESTAMP_NS
        );

        FD = open(
                PCAP->PATH,
                O_WRONLY | O_CREAT | O_EXCL,
                0600
        );

        if (FD < 0) {
                return ABNORMAL;
        }

        PCAP->FILE_HANDLE = fdopen(FD, "wb");
        if (!PCAP->FILE_HANDLE) {
                close(FD);
                return ABNORMAL;
        }

        HEADER.MAGIC = KPCAP_MAGIC_NS;
        HEADER.VERSION_MAJOR = 2;
        HEADER.VERSION_MINOR = 4;
        HEADER.THIS_ZONE = 0;
        HEADER.SIGFIGS = 0;
        HEADER.SNAPLEN = 65535;
        HEADER.NETWORK = KPCAP_LINKTYPE_ETHERNET;

        if (
                fwrite(
                        &HEADER,
                        sizeof(HEADER),
                        1,
                        PCAP->FILE_HANDLE
                ) != 1
        ) {
                fclose(PCAP->FILE_HANDLE);
                PCAP->FILE_HANDLE = NULL;
                remove(PCAP->PATH);
                return ABNORMAL;
        }

        PCAP->SCAN_TIMESTAMP_NS = SCAN_TIMESTAMP_NS;
        PCAP->PACKET_COUNT = 0;
        PCAP->NETWORK = KPCAP_LINKTYPE_ETHERNET;
        KWIRE_ACTIVE_PCAP = PCAP;
        return NORMAL;
}

// @@ Scanner evidence PCAPs are transaction-owned and must not become the
// legacy active capture used by kwire_rx_accept() in ping/DNS paths.
static inline int kwire_pcap_open_detached(
        _carry_forward * _prog_data,
        KPCAP * PCAP,
        const char * SCAN_TYPE,
        const unsigned char * TID,
        uint64_t SCAN_TIMESTAMP_NS
) {
        int STATUS;

        STATUS = kwire_pcap_open(
                _prog_data,
                PCAP,
                SCAN_TYPE,
                TID,
                SCAN_TIMESTAMP_NS
        );

        if (
                STATUS == NORMAL
                && KWIRE_ACTIVE_PCAP == PCAP
        ) {
                KWIRE_ACTIVE_PCAP = NULL;
        }

        return STATUS;
}

static inline int kwire_pcap_set_network(
        KPCAP * PCAP,
        uint32_t NETWORK
) {
        long POSITION;

        if (!PCAP || !PCAP->FILE_HANDLE) {
                return ABNORMAL;
        }

        if (PCAP->PACKET_COUNT > 0) {
                return PCAP->NETWORK == NETWORK
                        ? NORMAL
                        : ABNORMAL;
        }

        if (PCAP->NETWORK == NETWORK) {
                return NORMAL;
        }

        POSITION = ftell(PCAP->FILE_HANDLE);
        if (POSITION < 0) {
                return ABNORMAL;
        }

        if (
                fseek(
                        PCAP->FILE_HANDLE,
                        (long)offsetof(KPCAP_GLOBAL_HEADER, NETWORK),
                        SEEK_SET
                ) != NORMAL
        ) {
                return ABNORMAL;
        }

        if (
                fwrite(
                        &NETWORK,
                        sizeof(NETWORK),
                        1,
                        PCAP->FILE_HANDLE
                ) != 1
        ) {
                return ABNORMAL;
        }

        if (fflush(PCAP->FILE_HANDLE) != NORMAL) {
                return ABNORMAL;
        }

        if (fseek(PCAP->FILE_HANDLE, POSITION, SEEK_SET) != NORMAL) {
                return ABNORMAL;
        }

        PCAP->NETWORK = NETWORK;
        return NORMAL;
}

static inline int kwire_pcap_packet(
        KPCAP * PCAP,
        const uint8_t * FRAME,
        size_t CAPTURE_LENGTH,
        size_t WIRE_LENGTH,
        uint64_t TIMESTAMP_NS
) {
        KPCAP_PACKET_HEADER HEADER;

        if (
                !PCAP
                || !PCAP->FILE_HANDLE
                || !FRAME
                || CAPTURE_LENGTH == 0
                || TIMESTAMP_NS == 0
        ) {
                return ABNORMAL;
        }

        memset(&HEADER, 0x00, sizeof(HEADER));

        HEADER.TIMESTAMP_SECONDS = (uint32_t)(
                TIMESTAMP_NS / 1000000000ULL
        );
        HEADER.TIMESTAMP_NANOSECONDS = (uint32_t)(
                TIMESTAMP_NS % 1000000000ULL
        );
        HEADER.CAPTURE_LENGTH = (uint32_t)CAPTURE_LENGTH;
        HEADER.WIRE_LENGTH = (uint32_t)(
                WIRE_LENGTH ? WIRE_LENGTH : CAPTURE_LENGTH
        );

        if (
                fwrite(
                        &HEADER,
                        sizeof(HEADER),
                        1,
                        PCAP->FILE_HANDLE
                ) != 1
        ) {
                return ABNORMAL;
        }

        if (
                fwrite(
                        FRAME,
                        1,
                        CAPTURE_LENGTH,
                        PCAP->FILE_HANDLE
                ) != CAPTURE_LENGTH
        ) {
                return ABNORMAL;
        }

        if (fflush(PCAP->FILE_HANDLE) != NORMAL) {
                return ABNORMAL;
        }

        PCAP->PACKET_COUNT++;
        return NORMAL;
}

static inline void kwire_pcap_close(KPCAP * PCAP) {
        if (!PCAP || !PCAP->FILE_HANDLE) {
                return;
        }

        fflush(PCAP->FILE_HANDLE);
        fsync(fileno(PCAP->FILE_HANDLE));
        fclose(PCAP->FILE_HANDLE);
        PCAP->FILE_HANDLE = NULL;

        if (KWIRE_ACTIVE_PCAP == PCAP) {
                KWIRE_ACTIVE_PCAP = NULL;
        }
}

static inline int kwire_pcap_append_capture(
        _carry_forward * _prog_data,
        const char * SCAN_TYPE,
        const unsigned char * TID,
        uint64_t SCAN_TIMESTAMP_NS,
        const nosix_capture_t * CAPTURE
) {
        KPCAP PCAP;
        KPCAP_GLOBAL_HEADER HEADER;
        FILE * FILE_HANDLE;
        uint32_t LINKTYPE;
        int WRITTEN;

        if (
                !_prog_data
                || !SCAN_TYPE
                || !TID
                || SCAN_TIMESTAMP_NS == 0
                || !CAPTURE
                || !CAPTURE->frame.data
                || CAPTURE->frame.length == 0
        ) {
                return ABNORMAL;
        }

        // @@ This capture has already been transaction-matched by the caller.
        // Runtime bandwidth accounting must not depend on evidence persistence.
        _prog_data->total_rx_bytes += (
                CAPTURE->wire_length
                ? CAPTURE->wire_length
                : CAPTURE->frame.length
        );

        memset(&PCAP, 0x00, sizeof(PCAP));
        memset(&HEADER, 0x00, sizeof(HEADER));

        WRITTEN = snprintf(
                PCAP.PATH,
                sizeof(PCAP.PATH),
                "%s/%s%s/%s/%s-%s-%" PRIu64 ".pcap",
                _prog_data->wd,
                KAMI_USER_PROJECTS_DIR,
                _prog_data->active_project,
                TID,
                SCAN_TYPE,
                TID,
                SCAN_TIMESTAMP_NS
        );

        if (WRITTEN < 0 || WRITTEN >= (int)sizeof(PCAP.PATH)) {
                return ABNORMAL;
        }

        FILE_HANDLE = fopen(PCAP.PATH, "r+b");
        if (!FILE_HANDLE) {
                return ABNORMAL;
        }

        if (fread(&HEADER, sizeof(HEADER), 1, FILE_HANDLE) != 1) {
                fclose(FILE_HANDLE);
                return ABNORMAL;
        }

        LINKTYPE = (CAPTURE->flags & NOSIX_CAPTURE_IPV4_LOCAL)
                ? KPCAP_LINKTYPE_RAW_IPV4
                : KPCAP_LINKTYPE_ETHERNET;

        if (
                HEADER.MAGIC != KPCAP_MAGIC_NS
                || HEADER.VERSION_MAJOR != 2
                || HEADER.VERSION_MINOR != 4
                || HEADER.NETWORK != LINKTYPE
                || fseek(FILE_HANDLE, 0, SEEK_END) != NORMAL
        ) {
                fclose(FILE_HANDLE);
                return ABNORMAL;
        }

        PCAP.FILE_HANDLE = FILE_HANDLE;
        PCAP.SCAN_TIMESTAMP_NS = SCAN_TIMESTAMP_NS;
        PCAP.PACKET_COUNT = 1;
        PCAP.NETWORK = HEADER.NETWORK;

        if (
                kwire_pcap_packet(
                        &PCAP,
                        CAPTURE->frame.data,
                        CAPTURE->frame.length,
                        CAPTURE->wire_length,
                        CAPTURE->timestamp_ns
                                ? CAPTURE->timestamp_ns
                                : kscan_now_ns()
                ) != NORMAL
        ) {
                kwire_pcap_close(&PCAP);
                return ABNORMAL;
        }

        kwire_pcap_close(&PCAP);
        return NORMAL;
}

// @@ TX is always Kami-owned. Promiscuous RX is silent until a caller matches it to Kami.
static inline void kwire_notice_transmission(
        size_t LENGTH
) {
        kui_add_line_and_render(
                NOTICE_TRANSMISSION
                "Transmitted packet with "
                RGB_COLOR_BRIGHT_GREEN
                "%zu"
                ANSI_COLOR_RESET
                " bytes.",
                LENGTH
        );
}

static inline void kwire_notice_packet_transmission(
        const nosix_tx_packet_t * PACKET,
        size_t LENGTH
) {
        const uint8_t * UPPER;
        uint16_t PORT;

        if (
                !PACKET
                || !PACKET->upper_layer
                || PACKET->upper_layer_length < 4
        ) {
                kwire_notice_transmission(LENGTH);
                return;
        }

        UPPER = PACKET->upper_layer;
        PORT = (uint16_t)(
                ((uint16_t)UPPER[2] << 8)
                | (uint16_t)UPPER[3]
        );

        if (PACKET->ip_protocol == NOSIX_IPPROTO_TCP) {
                kui_add_line_and_render(
                        NOTICE_TRANSMISSION
                        "Transmitted "
                        ANSI_COLOR_CYAN
                        "TCP/%u"
                        ANSI_COLOR_RESET
                        " packet with "
                        RGB_COLOR_BRIGHT_GREEN
                        "%zu"
                        ANSI_COLOR_RESET
                        " bytes.",
                        PORT,
                        LENGTH
                );
                return;
        }

        if (PACKET->ip_protocol == NOSIX_IPPROTO_UDP) {
                kui_add_line_and_render(
                        NOTICE_TRANSMISSION
                        "Transmitted "
                        ANSI_COLOR_CYAN
                        "UDP/%u"
                        ANSI_COLOR_RESET
                        " packet with "
                        RGB_COLOR_BRIGHT_GREEN
                        "%zu"
                        ANSI_COLOR_RESET
                        " bytes.",
                        PORT,
                        LENGTH
                );
                return;
        }

        kwire_notice_transmission(LENGTH);
}

static inline nosix_status_t kwire_write(
        _carry_forward * _prog_data,
        KPCAP * PCAP,
        const nosix_tx_packet_t * PACKET,
        size_t * FRAME_LENGTH
) {
        uint8_t FRAME_DATA[OUT_BLOCK];
        nosix_frame_t FRAME;
        nosix_status_t STATUS;
        nosix_tx_surface_t SURFACE;
        uint64_t TIMESTAMP_NS;
        uint32_t LINKTYPE;

        if (!_prog_data || !PCAP || !PACKET) {
                return NOSIX_ERR_ARGUMENT;
        }

        if (_prog_data->gprof.rx_state == RXS_TRANSACTION) {
                STATUS = nosix_capture_reset(
                        _prog_data->nosix_net
                );

                if (STATUS != NOSIX_OK) {
                        return STATUS;
                }
        }

        memset(FRAME_DATA, 0x00, sizeof(FRAME_DATA));
        memset(&FRAME, 0x00, sizeof(FRAME));

        FRAME.data = FRAME_DATA;
        FRAME.capacity = sizeof(FRAME_DATA);

        STATUS = nosix_write_frame(
                _prog_data->nosix_net,
                PACKET,
                &FRAME,
                FRAME_LENGTH
        );

        if (STATUS != NOSIX_OK) {
                return STATUS;
        }

        // @@ NOSIX has already emitted this packet. Count it even if evidence
        // persistence fails later in the transaction.
        _prog_data->total_tx_bytes += FRAME.length;

        SURFACE = nosix_last_tx_surface(
                _prog_data->nosix_net
        );

        LINKTYPE = SURFACE == NOSIX_TX_SURFACE_IPV4_LOCAL
                ? KPCAP_LINKTYPE_RAW_IPV4
                : KPCAP_LINKTYPE_ETHERNET;

        if (kwire_pcap_set_network(PCAP, LINKTYPE) != NORMAL) {
                return NOSIX_ERR_SYSTEM;
        }

        TIMESTAMP_NS = kscan_now_ns();
        if (
                kwire_pcap_packet(
                        PCAP,
                        FRAME.data,
                        FRAME.length,
                        FRAME.length,
                        TIMESTAMP_NS
                ) != NORMAL
        ) {
                return NOSIX_ERR_SYSTEM;
        }

        return NOSIX_OK;
}

// @@ Scan scheduler TX path. KSCAN owns ordering/rate/pending limits; KWIRE owns NOSIX I/O.
static inline nosix_status_t kwire_scan_write(
        _carry_forward * _prog_data,
        const nosix_tx_packet_t * PACKET,
        uint8_t * FRAME_DATA,
        size_t FRAME_CAPACITY,
        size_t * FRAME_LENGTH
) {
        nosix_frame_t FRAME;
        nosix_status_t STATUS;
        size_t WRITTEN = 0;

        if (
                !_prog_data
                || !PACKET
                || !_prog_data->nosix_net
                || !FRAME_DATA
                || FRAME_CAPACITY == 0
        ) {
                return NOSIX_ERR_ARGUMENT;
        }

        memset(&FRAME, 0x00, sizeof(FRAME));
        FRAME.data = FRAME_DATA;
        FRAME.capacity = FRAME_CAPACITY;

        STATUS = nosix_write_frame(
                _prog_data->nosix_net,
                PACKET,
                &FRAME,
                &WRITTEN
        );

        if (STATUS != NOSIX_OK) {
                return STATUS;
        }

        kwire_notice_packet_transmission(
                PACKET,
                FRAME.length
        );
        _prog_data->total_tx_bytes += FRAME.length;

        if (FRAME_LENGTH) {
                *FRAME_LENGTH = FRAME.length;
        }

        return NOSIX_OK;
}

static inline nosix_tx_surface_t kwire_scan_last_tx_surface(
        _carry_forward * _prog_data
) {
        if (!_prog_data || !_prog_data->nosix_net) {
                return NOSIX_TX_SURFACE_NONE;
        }

        return nosix_last_tx_surface(
                _prog_data->nosix_net
        );
}

static inline nosix_status_t kwire_scan_lock_auto_interface(
        _carry_forward * _prog_data
) {
        if (!_prog_data || !_prog_data->nosix_net) {
                return NOSIX_ERR_ARGUMENT;
        }

        return nosix_lock_auto_interface(
                _prog_data->nosix_net
        );
}

static inline nosix_status_t kwire_scan_restore_auto_interface(
        _carry_forward * _prog_data
) {
        if (!_prog_data || !_prog_data->nosix_net) {
                return NOSIX_ERR_ARGUMENT;
        }

        return nosix_restore_auto_interface(
                _prog_data->nosix_net
        );
}

static inline nosix_status_t kwire_read(
        _carry_forward * _prog_data,
        KPCAP * PCAP,
        nosix_capture_t * CAPTURE
) {
        if (!_prog_data || !PCAP || !CAPTURE) {
                return NOSIX_ERR_ARGUMENT;
        }

        // @@ Promiscuous capture sees traffic that does not belong to Kami.
        // RX notices are emitted only by the caller after transaction matching.
        return nosix_read(
                _prog_data->nosix_net,
                CAPTURE
        );
}

static inline nosix_status_t kwire_scan_read(
        _carry_forward * _prog_data,
        nosix_capture_t * CAPTURE
) {
        if (!_prog_data || !_prog_data->nosix_net || !CAPTURE) {
                return NOSIX_ERR_ARGUMENT;
        }

        // @@ Promiscuous capture sees traffic that does not belong to Kami.
        // RX notices are emitted only after the scanner matches a transaction.
        return nosix_read(
                _prog_data->nosix_net,
                CAPTURE
        );
}

static inline void kwire_rx_accept(
        _carry_forward * _prog_data,
        const nosix_capture_t * CAPTURE
) {
        uint64_t TIMESTAMP_NS;
        uint32_t LINKTYPE;

        if (
                !_prog_data
                || !CAPTURE
                || CAPTURE->frame.length == 0
        ) {
                return;
        }

        // @@ Caller invokes this only after matching the packet to Kami.
        // Count accepted network bytes before attempting PCAP persistence.
        _prog_data->total_rx_bytes += (
                CAPTURE->wire_length
                ? CAPTURE->wire_length
                : CAPTURE->frame.length
        );

        if (!KWIRE_ACTIVE_PCAP) {
                return;
        }

        LINKTYPE = (CAPTURE->flags & NOSIX_CAPTURE_IPV4_LOCAL)
                ? KPCAP_LINKTYPE_RAW_IPV4
                : KPCAP_LINKTYPE_ETHERNET;

        if (
                kwire_pcap_set_network(
                        KWIRE_ACTIVE_PCAP,
                        LINKTYPE
                ) != NORMAL
        ) {
                return;
        }

        TIMESTAMP_NS = CAPTURE->timestamp_ns;
        if (TIMESTAMP_NS == 0) {
                TIMESTAMP_NS = kscan_now_ns();
        }

        if (
                kwire_pcap_packet(
                        KWIRE_ACTIVE_PCAP,
                        CAPTURE->frame.data,
                        CAPTURE->frame.length,
                        CAPTURE->wire_length,
                        TIMESTAMP_NS
                ) != NORMAL
        ) {
                return;
        }

}

static inline nosix_status_t kwire_injection(
        _carry_forward * _prog_data,
        KPCAP * PCAP,
        const nosix_injection_t * INJECTION
) {
        nosix_status_t STATUS;
        uint64_t TIMESTAMP_NS;

        if (!_prog_data || !PCAP || !INJECTION) {
                return NOSIX_ERR_ARGUMENT;
        }

        STATUS = nosix_injection(
                _prog_data->nosix_net,
                INJECTION
        );

        if (STATUS != NOSIX_OK) {
                return STATUS;
        }

        // @@ Injection completed on the network before evidence handling.
        _prog_data->total_tx_bytes += INJECTION->frame_length;

        kwire_notice_transmission(INJECTION->frame_length);

        if (
                kwire_pcap_set_network(
                        PCAP,
                        KPCAP_LINKTYPE_ETHERNET
                ) != NORMAL
        ) {
                return NOSIX_ERR_SYSTEM;
        }

        TIMESTAMP_NS = kscan_now_ns();
        if (
                kwire_pcap_packet(
                        PCAP,
                        INJECTION->frame,
                        INJECTION->frame_length,
                        INJECTION->frame_length,
                        TIMESTAMP_NS
                ) != NORMAL
        ) {
                return NOSIX_ERR_SYSTEM;
        }

        return NOSIX_OK;
}

#endif
