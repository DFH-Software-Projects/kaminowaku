// Copyright 2026 Jamison A. Drapeau
// Phase 4: public NOSIX L3 status, surface mapping and mixed-PCAP RX proof.
#define _POSIX_C_SOURCE 200809L
#include "kwire.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(NOSIX_ABI_VERSION_MAJOR == 1U, "NOSIX 1.x required");
_Static_assert(NOSIX_ABI_VERSION_MINOR == 5U, "NOSIX 1.5 required");
_Static_assert(NOSIX_ERR_L3_FALLBACK_FAILED == -11, "L3 status drift");
_Static_assert(NOSIX_TX_SURFACE_IPV4_L3 == 3, "L3 surface drift");
_Static_assert(SCAN_RESULT_L3_FALLBACK == (1U << 6), "Result bit drift");
_Static_assert(NOSIX_CAPTURE_IPV4_L3 == (1U << 2), "RX flag drift");

uint64_t kscan_now_ns(void) {
        return UINT64_C(1700000000000000000);
}

int main(void) {
        const uint8_t ipv4[28] = {
                0x45, 0, 0, 28, 0, 1, 0, 0, 64, 1, 0, 0,
                192, 0, 2, 1, 192, 0, 2, 2,
                0, 0, 0, 0, 0x4b, 0x57, 0, 1
        };
        nosix_capture_t capture;
        KPCAP base;
        KPCAP_GLOBAL_HEADER header;
        KPCAP_PACKET_HEADER packet_header;
        char folder[] = "/tmp/kami-betav3-XXXXXX";
        char sibling[MAX_PATH];
        char check[sizeof(ipv4)];
        FILE *fh;

        assert(kwire_tx_pcap_linktype(NOSIX_TX_SURFACE_ETHERNET)
                == KPCAP_LINKTYPE_ETHERNET);
        assert(kwire_tx_pcap_linktype(NOSIX_TX_SURFACE_IPV4_LOCAL)
                == KPCAP_LINKTYPE_RAW_IPV4);
        assert(kwire_tx_pcap_linktype(NOSIX_TX_SURFACE_IPV4_L3)
                == KPCAP_LINKTYPE_RAW_IPV4);
        assert(kwire_tx_pcap_linktype(NOSIX_TX_SURFACE_NONE) == 0);

        memset(&capture, 0, sizeof(capture));
        capture.frame.data = (uint8_t *)(void *)ipv4;
        capture.frame.length = sizeof(ipv4);
        capture.wire_length = sizeof(ipv4);
        capture.timestamp_ns = kscan_now_ns();
        capture.flags = NOSIX_CAPTURE_IPV4_L3;
        assert(kwire_rx_pcap_linktype(&capture) == KPCAP_LINKTYPE_RAW_IPV4);
        capture.flags = NOSIX_CAPTURE_IPV4_LOCAL;
        assert(kwire_rx_pcap_linktype(&capture) == KPCAP_LINKTYPE_RAW_IPV4);
        capture.flags = 0;
        assert(kwire_rx_pcap_linktype(&capture) == KPCAP_LINKTYPE_ETHERNET);

        /* When RX has no Ethernet header but TX PCAP uses Ethernet,
         * the matched packet goes into a separate DLT_RAW file.
         */
        capture.flags = NOSIX_CAPTURE_IPV4_L3;
        if (!mkdtemp(folder)) return 1;

        memset(&base, 0, sizeof(base));
        if (snprintf(base.PATH, sizeof(base.PATH),
                     "%s/probe.pcap", folder) >= (int)sizeof(base.PATH)) {
                return 1;
        }
        if (snprintf(sibling, sizeof(sibling),
                     "%s/probe-rx-ipv4.pcap", folder) >= (int)sizeof(sibling)) {
                return 1;
        }

        assert(kwire_pcap_alternate_rx(
                &base, &capture, KPCAP_LINKTYPE_RAW_IPV4
        ) == NORMAL);
        fh = fopen(sibling, "rb");
        assert(fh != NULL);
        assert(fread(&header, sizeof(header), 1, fh) == 1);
        assert(header.MAGIC == KPCAP_MAGIC_NS);
        assert(header.NETWORK == KPCAP_LINKTYPE_RAW_IPV4);
        assert(fread(&packet_header, sizeof(packet_header), 1, fh) == 1);
        assert(packet_header.CAPTURE_LENGTH == sizeof(ipv4));
        assert(fread(check, 1, sizeof(check), fh) == sizeof(check));
        assert(memcmp(check, ipv4, sizeof(ipv4)) == 0);
        fclose(fh);
        remove(sibling);
        rmdir(folder);
        puts("BetaV3 NOSIX L3 mappings and mixed-link PCAP: PASS");
        return 0;
}
