// Copyright 2026 Jamison A. Drapeau
#include "book_session.h"
#include "book_audit.h"
#include <nosix_poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define BOOK_PCAP_HEADER_SIZE 24U
#define BOOK_PCAP_PACKET_HEADER_SIZE 16U
#define BOOK_PCAP_COLLISION_LIMIT 100U
#define BOOK_CAPTURE_DRAIN_LIMIT 4096U
#define BOOK_CAPTURE_BUFFER 65535U

// @@ Total immutable book evidence written during this Kaminowaku runtime
static uint64_t BOOK_CAPTURE_RUNTIME_BYTES = 0;

// @@ Current realtime in nanoseconds
static uint64_t books_now_ns(void) {
        struct timespec ts;

        memset(&ts, 0x00, sizeof(ts));
        if (clock_gettime(CLOCK_REALTIME, &ts) != NORMAL) return 0;
        return ((uint64_t)ts.tv_sec * 1000000000ULL) + (uint64_t)ts.tv_nsec;
}

// @@ Complete write helper
static int books_write_all(
        int fd,
        const unsigned char * data,
        c_size_t length
) {
        c_size_t offset = 0;

        if (fd < 0 || !data) return ABNORMAL;

        while (offset < length) {
                ssize_t written = write(fd, data + offset, length - offset);

                if (written < 0) {
                        if (errno == EINTR) continue;
                        return ABNORMAL;
                }
                if (written == 0) return ABNORMAL;
                offset += (c_size_t)written;
        }

        return NORMAL;
}

static void books_u32le(unsigned char * out, uint32_t value) {
        out[0] = (unsigned char)(value & 0xffU);
        out[1] = (unsigned char)((value >> 8) & 0xffU);
        out[2] = (unsigned char)((value >> 16) & 0xffU);
        out[3] = (unsigned char)((value >> 24) & 0xffU);
}

// @@ Profile capture limits apply to each book transaction and the runtime total
static int books_capture_limit_allows(
        const BOOK_SESSION * session,
        uint64_t addition
) {
        uint64_t per_session;
        uint64_t total;

        if (!session || !session->prog_data) return ISFALSE;

        per_session = session->prog_data->gprof.limit_max_capture_bytes_per_tx;
        total = session->prog_data->gprof.limit_max_capture_bytes_total;

        if (
                per_session > 0
                && (
                        addition > UINT64_MAX - session->pcap_bytes
                        || session->pcap_bytes + addition > per_session
                )
        ) return ISFALSE;

        if (
                total > 0
                && (
                        addition > UINT64_MAX - BOOK_CAPTURE_RUNTIME_BYTES
                        || BOOK_CAPTURE_RUNTIME_BYTES + addition > total
                )
        ) return ISFALSE;

        return ISTRUE;
}

static void books_capture_account(
        BOOK_SESSION * session,
        uint64_t bytes
) {
        if (!session) return;
        session->pcap_bytes += bytes;
        BOOK_CAPTURE_RUNTIME_BYTES += bytes;
}

// @@ Create a valid classic-PCAP global header before network permission exists
static int books_write_pcap_header(BOOK_SESSION * session) {
        unsigned char header[BOOK_PCAP_HEADER_SIZE];
        uint32_t snaplength;

        if (!session || session->pcap_fd < 0 || !session->prog_data) return ABNORMAL;

        if (books_capture_limit_allows(session, BOOK_PCAP_HEADER_SIZE) != ISTRUE) {
                session->termination = BOOK_TERM_LIMIT_ERROR;
                books_audit_action(session, "ERROR", "reason=capture_bytes_limit phase=pcap_header");
                return ABNORMAL;
        }

        memset(header, 0x00, sizeof(header));
        header[0] = 0xd4;
        header[1] = 0xc3;
        header[2] = 0xb2;
        header[3] = 0xa1;
        header[4] = 0x02;
        header[5] = 0x00;
        header[6] = 0x04;
        header[7] = 0x00;

        snaplength = session->prog_data->gprof.rx_snaplength > 0
                ? (uint32_t)session->prog_data->gprof.rx_snaplength
                : 65535U;

        books_u32le(header + 16, snaplength);
        books_u32le(header + 20, BOOK_PCAP_LINKTYPE_ETHERNET);
        session->pcap_linktype = BOOK_PCAP_LINKTYPE_ETHERNET;

        if (books_write_all(session->pcap_fd, header, sizeof(header)) != NORMAL) {
                return ABNORMAL;
        }

        books_capture_account(session, sizeof(header));
        return NORMAL;
}

// @@ Failed pre-action capture setup leaves no false immutable artifact
static void books_capture_setup_discard(BOOK_SESSION * session) {
        if (!session) return;

        if (session->pcap_fd >= 0) {
                close(session->pcap_fd);
                session->pcap_fd = -1;
        }

        if (session->pcap_path[0] != 0x00) {
                unlink((const char*)session->pcap_path);
                memset(session->pcap_path, 0x00, sizeof(session->pcap_path));
        }

        if (session->pcap_bytes <= BOOK_CAPTURE_RUNTIME_BYTES) {
                BOOK_CAPTURE_RUNTIME_BYTES -= session->pcap_bytes;
        } else {
                BOOK_CAPTURE_RUNTIME_BYTES = 0;
        }

        session->pcap_bytes = 0;
        session->pcap_packets = 0;
        session->capture_active = ISFALSE;
}

// @@ Classic PCAP has one file-wide link type; it may change only before record one
static int books_pcap_set_linktype(
        BOOK_SESSION * session,
        uint32_t linktype
) {
        unsigned char encoded[4];
        ssize_t written;

        if (!session || session->pcap_fd < 0) return ABNORMAL;
        if (session->pcap_linktype == linktype) return NORMAL;
        if (session->pcap_packets != 0) return ABNORMAL;

        memset(encoded, 0x00, sizeof(encoded));
        books_u32le(encoded, linktype);

        do {
                written = pwrite(session->pcap_fd, encoded, sizeof(encoded), 20);
        } while (written < 0 && errno == EINTR);

        if (written != (ssize_t)sizeof(encoded)) return ABNORMAL;
        session->pcap_linktype = linktype;
        return NORMAL;
}

// @@ Append one captured frame/packet to immutable session evidence
static int books_pcap_capture(
        BOOK_SESSION * session,
        const nosix_capture_t * capture
) {
        unsigned char header[BOOK_PCAP_PACKET_HEADER_SIZE];
        uint64_t timestamp_ns;
        uint64_t record_bytes;
        uint32_t linktype;
        uint32_t capture_length;
        uint32_t wire_length;

        if (
                !session
                || !capture
                || session->pcap_fd < 0
                || !capture->frame.data
                || capture->frame.length == 0
        ) return ABNORMAL;

        linktype = (capture->flags & NOSIX_CAPTURE_IPV4_LOCAL)
                ? BOOK_PCAP_LINKTYPE_RAW_IPV4
                : BOOK_PCAP_LINKTYPE_ETHERNET;

        if (books_pcap_set_linktype(session, linktype) != NORMAL) return ABNORMAL;

        capture_length = capture->frame.length > UINT32_MAX
                ? UINT32_MAX
                : (uint32_t)capture->frame.length;
        wire_length = capture->wire_length > UINT32_MAX
                ? UINT32_MAX
                : (uint32_t)(
                        capture->wire_length
                                ? capture->wire_length
                                : capture->frame.length
                );
        record_bytes = BOOK_PCAP_PACKET_HEADER_SIZE + (uint64_t)capture_length;

        if (books_capture_limit_allows(session, record_bytes) != ISTRUE) {
                session->termination = BOOK_TERM_LIMIT_ERROR;
                session->capture_active = ISFALSE;
                books_audit_action(session, "ERROR", "reason=capture_bytes_limit phase=packet");
                return ABNORMAL;
        }

        timestamp_ns = capture->timestamp_ns ? capture->timestamp_ns : books_now_ns();
        memset(header, 0x00, sizeof(header));
        books_u32le(header + 0, (uint32_t)(timestamp_ns / 1000000000ULL));
        books_u32le(header + 4, (uint32_t)((timestamp_ns % 1000000000ULL) / 1000ULL));
        books_u32le(header + 8, capture_length);
        books_u32le(header + 12, wire_length);

        if (books_write_all(session->pcap_fd, header, sizeof(header)) != NORMAL) return ABNORMAL;
        if (books_write_all(session->pcap_fd, capture->frame.data, capture_length) != NORMAL) return ABNORMAL;

        session->pcap_packets++;
        books_capture_account(session, record_bytes);
        return NORMAL;
}

// @@ Normalize Ethernet/local capture metadata for transaction matching
static int books_capture_network(
        const nosix_capture_t * capture,
        const uint8_t ** frame,
        size_t * length,
        size_t * network_offset,
        uint16_t * ethertype
) {
        if (!capture || !frame || !length || !network_offset || !ethertype) return ABNORMAL;
        if (!capture->frame.data || capture->frame.length == 0) return ABNORMAL;

        *frame = capture->frame.data;
        *length = capture->frame.length;
        *network_offset = 0;
        *ethertype = 0;

        if (capture->flags & NOSIX_CAPTURE_IPV4_LOCAL) {
                *ethertype = 0x0800;
                return NORMAL;
        }

        if (*length < 14) return ABNORMAL;
        *ethertype = (uint16_t)(((uint16_t)(*frame)[12] << 8) | (*frame)[13]);
        *network_offset = 14;

        for (int tag = 0; tag < 2; tag++) {
                if (*ethertype != 0x8100 && *ethertype != 0x88A8) break;
                if (*length < *network_offset + 4) return ABNORMAL;
                *ethertype = (uint16_t)(
                        ((uint16_t)(*frame)[*network_offset + 2] << 8)
                        | (*frame)[*network_offset + 3]
                );
                *network_offset += 4;
        }

        return NORMAL;
}

// @@ Match TCP/UDP payload traffic to target + remote port + learned local port
static int books_capture_matches_transport(
        BOOK_SESSION * session,
        const nosix_capture_t * capture,
        int * direction_out
) {
        const uint8_t * frame;
        size_t length;
        size_t network_offset;
        size_t transport_offset;
        uint16_t ethertype;
        uint16_t source_port;
        uint16_t destination_port;
        uint16_t candidate_local_port;
        uint8_t expected_protocol;
        int direction = 0;

        if (direction_out) *direction_out = 0;

        if (
                !session
                || !session->target
                || !session->target->PETAL
                || session->destination_port == 0
                || (
                        session->transport != BOOK_TRANSPORT_TCP
                        && session->transport != BOOK_TRANSPORT_UDP
                )
                || books_capture_network(
                        capture,
                        &frame,
                        &length,
                        &network_offset,
                        &ethertype
                ) != NORMAL
        ) return ISFALSE;

        expected_protocol = session->transport == BOOK_TRANSPORT_TCP
                ? NOSIX_IPPROTO_TCP
                : NOSIX_IPPROTO_UDP;

        if (ethertype == 0x0800 && session->address_family == NOSIX_ADDRESS_IPV4) {
                uint8_t target[4];
                size_t ip_header_length;

                if (length < network_offset + 20) return ISFALSE;
                if ((frame[network_offset] >> 4) != 4) return ISFALSE;
                if (frame[network_offset + 9] != expected_protocol) return ISFALSE;

                ip_header_length = (size_t)(frame[network_offset] & 0x0fU) * 4U;
                if (ip_header_length < 20 || length < network_offset + ip_header_length + 4) return ISFALSE;

                memset(target, 0x00, sizeof(target));
                if (inet_pton(AF_INET, (const char*)session->target->PETAL->IPV4, target) != 1) return ISFALSE;

                if (memcmp(frame + network_offset + 16, target, sizeof(target)) == MATCH) direction = 1;
                else if (memcmp(frame + network_offset + 12, target, sizeof(target)) == MATCH) direction = -1;
                else return ISFALSE;

                transport_offset = network_offset + ip_header_length;
        } else if (ethertype == 0x86DD && session->address_family == NOSIX_ADDRESS_IPV6) {
                uint8_t target[16];

                if (length < network_offset + 44) return ISFALSE;
                if ((frame[network_offset] >> 4) != 6) return ISFALSE;

                // Initial release accepts direct TCP/UDP next-header only.
                if (frame[network_offset + 6] != expected_protocol) return ISFALSE;

                memset(target, 0x00, sizeof(target));
                if (inet_pton(AF_INET6, (const char*)session->target->PETAL->IPV6, target) != 1) return ISFALSE;

                if (memcmp(frame + network_offset + 24, target, sizeof(target)) == MATCH) direction = 1;
                else if (memcmp(frame + network_offset + 8, target, sizeof(target)) == MATCH) direction = -1;
                else return ISFALSE;

                transport_offset = network_offset + 40;
        } else {
                return ISFALSE;
        }

        source_port = (uint16_t)(
                ((uint16_t)frame[transport_offset] << 8)
                | frame[transport_offset + 1]
        );
        destination_port = (uint16_t)(
                ((uint16_t)frame[transport_offset + 2] << 8)
                | frame[transport_offset + 3]
        );

        if (direction > 0) {
                if (destination_port != session->destination_port) return ISFALSE;
                candidate_local_port = source_port;
        } else {
                if (source_port != session->destination_port) return ISFALSE;
                candidate_local_port = destination_port;
        }

        if (candidate_local_port == 0) return ISFALSE;
        if (session->local_port == 0) session->local_port = candidate_local_port;
        if (session->local_port != candidate_local_port) return ISFALSE;

        if (direction_out) *direction_out = direction;
        return ISTRUE;
}

// @@ Preserve attributable ARP, ICMP, and NDP setup/error evidence
static int books_capture_matches_supporting(
        const BOOK_SESSION * session,
        const nosix_capture_t * capture,
        int * direction_out
) {
        const uint8_t * frame;
        size_t length;
        size_t network_offset;
        uint16_t ethertype;

        if (direction_out) *direction_out = 0;

        if (
                !session
                || !session->target
                || !session->target->PETAL
                || books_capture_network(
                        capture,
                        &frame,
                        &length,
                        &network_offset,
                        &ethertype
                ) != NORMAL
        ) return ISFALSE;

        if (ethertype == 0x0806 && !(capture->flags & NOSIX_CAPTURE_IPV4_LOCAL)) {
                uint8_t target[4];
                const uint8_t * arp;

                if (length < network_offset + 28) return ISFALSE;
                if (session->target->PETAL->IPV4[0] == 0x00) return ISFALSE;
                arp = frame + network_offset;

                if (
                        arp[2] != 0x08
                        || arp[3] != 0x00
                        || arp[4] != 6
                        || arp[5] != 4
                ) return ISFALSE;

                if (inet_pton(AF_INET, (const char*)session->target->PETAL->IPV4, target) != 1) return ISFALSE;

                if (memcmp(arp + 14, target, sizeof(target)) == MATCH) {
                        if (direction_out) *direction_out = -1;
                        return ISTRUE;
                }

                if (memcmp(arp + 24, target, sizeof(target)) == MATCH) {
                        if (direction_out) *direction_out = 1;
                        return ISTRUE;
                }

                return ISFALSE;
        }

        if (ethertype == 0x0800 && session->address_family == NOSIX_ADDRESS_IPV4) {
                uint8_t target[4];

                if (length < network_offset + 20) return ISFALSE;
                if ((frame[network_offset] >> 4) != 4) return ISFALSE;
                if (frame[network_offset + 9] != NOSIX_IPPROTO_ICMP) return ISFALSE;
                if (inet_pton(AF_INET, (const char*)session->target->PETAL->IPV4, target) != 1) return ISFALSE;

                if (memcmp(frame + network_offset + 12, target, sizeof(target)) == MATCH) {
                        if (direction_out) *direction_out = -1;
                        return ISTRUE;
                }

                if (memcmp(frame + network_offset + 16, target, sizeof(target)) == MATCH) {
                        if (direction_out) *direction_out = 1;
                        return ISTRUE;
                }

                return ISFALSE;
        }

        if (ethertype == 0x86DD && session->address_family == NOSIX_ADDRESS_IPV6) {
                uint8_t target[16];
                size_t icmp_offset;
                uint8_t type;

                if (length < network_offset + 40) return ISFALSE;
                if ((frame[network_offset] >> 4) != 6) return ISFALSE;
                if (frame[network_offset + 6] != NOSIX_IPPROTO_ICMPV6) return ISFALSE;
                if (inet_pton(AF_INET6, (const char*)session->target->PETAL->IPV6, target) != 1) return ISFALSE;

                if (memcmp(frame + network_offset + 8, target, sizeof(target)) == MATCH) {
                        if (direction_out) *direction_out = -1;
                        return ISTRUE;
                }

                if (memcmp(frame + network_offset + 24, target, sizeof(target)) == MATCH) {
                        if (direction_out) *direction_out = 1;
                        return ISTRUE;
                }

                icmp_offset = network_offset + 40;
                if (length < icmp_offset + 24) return ISFALSE;
                type = frame[icmp_offset];

                if (
                        (type == 135 || type == 136)
                        && memcmp(frame + icmp_offset + 8, target, 16) == MATCH
                ) {
                        if (direction_out) {
                                *direction_out = type == 135 ? 1 : -1;
                        }
                        return ISTRUE;
                }
        }

        return ISFALSE;
}

const char * books_termination_name(book_termination_t termination) {
        switch (termination) {
                case BOOK_TERM_COMPLETE:            return "COMPLETE";
                case BOOK_TERM_BAILED:              return "BAILED";
                case BOOK_TERM_RUNTIME_ERROR:       return "RUNTIME_ERROR";
                case BOOK_TERM_TRANSPORT_ERROR:     return "TRANSPORT_ERROR";
                case BOOK_TERM_LIMIT_ERROR:         return "LIMIT_ERROR";
                case BOOK_TERM_INTERRUPTED:         return "INTERRUPTED";
                case BOOK_TERM_CAPTURE_SETUP_ERROR: return "CAPTURE_SETUP_ERROR";
                case BOOK_TERM_OUTPUT_COMMIT_ERROR: return "OUTPUT_COMMIT_ERROR";
                case BOOK_TERM_NONE:
                default:                            return "NONE";
        }
}

// @@ Convert the active target into one NOSIX destination
static nosix_status_t books_session_destination(
        BOOK_SESSION * session,
        nosix_address_t * destination
) {
        if (!session || !destination || !session->target || !session->target->PETAL) {
                return NOSIX_ERR_ARGUMENT;
        }

        memset(destination, 0x00, sizeof(*destination));

        if (session->target->PETAL->IPV4[0] != 0x00) {
                destination->family = NOSIX_ADDRESS_IPV4;
                if (
                        inet_pton(
                                AF_INET,
                                (const char*)session->target->PETAL->IPV4,
                                destination->bytes.ipv4
                        ) != 1
                ) return NOSIX_ERR_ADDRESS;
        } else if (session->target->PETAL->IPV6[0] != 0x00) {
                destination->family = NOSIX_ADDRESS_IPV6;
                if (
                        inet_pton(
                                AF_INET6,
                                (const char*)session->target->PETAL->IPV6,
                                destination->bytes.ipv6
                        ) != 1
                ) return NOSIX_ERR_ADDRESS;
        } else {
                return NOSIX_ERR_ADDRESS;
        }

        return NOSIX_OK;
}

// @@ Open the immutable transaction artifact before allowing book networking
int books_session_prepare(
        BOOK_SESSION * session,
        _carry_forward * _prog_data,
        const unsigned char * book_name,
        FLOWER * target,
        const char * target_directory
) {
        unsigned int collision = 0;
        int written;
        nosix_status_t status;

        if (
                !session
                || !_prog_data
                || !book_name
                || !target
                || !target->PETAL
                || !target_directory
                || target_directory[0] == 0x00
        ) return ABNORMAL;

        memset(session, 0x00, sizeof(*session));
        session->pcap_fd = -1;
        session->prog_data = _prog_data;
        session->target = target;
        session->session_id = books_now_ns();
        session->started_ns = session->session_id;
        session->state = BOOK_SESSION_CREATED;
        session->termination = BOOK_TERM_NONE;
        session->transport = BOOK_TRANSPORT_NONE;
        session->address_family = NOSIX_ADDRESS_NONE;

        snprintf((char*)session->book_name, sizeof(session->book_name), "%s", book_name);
        snprintf((char*)session->tid, sizeof(session->tid), "%s", target->TID);
        books_audit_action(session, "START", NULL);

        while (collision < BOOK_PCAP_COLLISION_LIMIT) {
                memset(session->pcap_path, 0x00, sizeof(session->pcap_path));

                // @@ Keep Book evidence filenames aligned with kwire PCAP naming:
                //    BOOKNAME-TID-EPOCH_NS.pcap
                // started_ns is CLOCK_REALTIME nanoseconds since the Unix epoch.
                written = collision == 0
                        ? snprintf(
                                (char*)session->pcap_path,
                                sizeof(session->pcap_path),
                                "%s%s%s-%s-%" PRIu64 ".pcap",
                                target_directory,
                                target_directory[strlen(target_directory) - 1] == '/' ? "" : "/",
                                book_name,
                                target->TID,
                                session->started_ns
                        )
                        : snprintf(
                                (char*)session->pcap_path,
                                sizeof(session->pcap_path),
                                "%s%s%s-%s-%" PRIu64 "-%u.pcap",
                                target_directory,
                                target_directory[strlen(target_directory) - 1] == '/' ? "" : "/",
                                book_name,
                                target->TID,
                                session->started_ns,
                                collision
                        );

                if (written <= 0 || (c_size_t)written >= sizeof(session->pcap_path)) {
                        session->termination = BOOK_TERM_CAPTURE_SETUP_ERROR;
                        books_audit_action(session, "ERROR", "reason=pcap_path");
                        return ABNORMAL;
                }

                session->pcap_fd = open(
                        (const char*)session->pcap_path,
                        O_WRONLY | O_CREAT | O_EXCL,
                        0600
                );

                if (session->pcap_fd >= 0) break;
                if (errno != EEXIST) break;
                collision++;
        }

        if (session->pcap_fd < 0) {
                session->termination = BOOK_TERM_CAPTURE_SETUP_ERROR;
                books_audit_action(session, "ERROR", "reason=pcap_open");
                return ABNORMAL;
        }

        if (books_write_pcap_header(session) != NORMAL) {
                if (session->termination == BOOK_TERM_NONE) {
                        session->termination = BOOK_TERM_CAPTURE_SETUP_ERROR;
                }
                books_audit_action(session, "ERROR", "reason=pcap_header");
                books_capture_setup_discard(session);
                return ABNORMAL;
        }

        if (!_prog_data->nosix_net) {
                session->termination = BOOK_TERM_CAPTURE_SETUP_ERROR;
                books_audit_action(session, "ERROR", "reason=nosix_offline");
                books_capture_setup_discard(session);
                return ABNORMAL;
        }

        status = nosix_capture_reset(_prog_data->nosix_net);
        _prog_data->nosix_status = status;
        if (status != NOSIX_OK) {
                session->termination = BOOK_TERM_CAPTURE_SETUP_ERROR;
                books_audit_action(session, "ERROR", "reason=capture_reset");
                books_capture_setup_discard(session);
                return ABNORMAL;
        }

        session->capture_active = ISTRUE;
        session->state = BOOK_SESSION_CAPTURE_ACTIVE;
        books_audit_action(session, "CAPTURE_ACTIVE", NULL);
        return NORMAL;
}

int books_session_network_allowed(const BOOK_SESSION * session) {
        if (!session) return ISFALSE;
        if (session->capture_active != ISTRUE) return ISFALSE;
        if (session->pcap_fd < 0) return ISFALSE;
        if (session->termination == BOOK_TERM_LIMIT_ERROR) return ISFALSE;
        if (session->state < BOOK_SESSION_CAPTURE_ACTIVE) return ISFALSE;
        if (session->state >= BOOK_SESSION_CLOSING) return ISFALSE;
        return ISTRUE;
}

void books_session_set_termination(
        BOOK_SESSION * session,
        book_termination_t termination
) {
        if (!session) return;
        session->termination = termination;
}

void books_session_note_network_action(BOOK_SESSION * session) {
        if (!session) return;
        if (books_session_network_allowed(session) != ISTRUE) return;
        session->network_actions++;
}

// @@ Drain matching transport and supporting traffic into the session PCAP
int books_session_capture_drain(
        BOOK_SESSION * session,
        int32_t first_wait_ms
) {
        uint8_t * frame_data;
        nosix_capture_t capture;
        nosix_status_t status;
        uint32_t reads = 0;
        int32_t timeout_ms;

        if (
                !session
                || !session->prog_data
                || !session->prog_data->nosix_net
                || session->pcap_fd < 0
                || session->termination == BOOK_TERM_LIMIT_ERROR
        ) return ABNORMAL;

        frame_data = malloc(BOOK_CAPTURE_BUFFER);
        if (!frame_data) return ABNORMAL;
        timeout_ms = first_wait_ms < 0 ? 0 : first_wait_ms;

        while (reads < BOOK_CAPTURE_DRAIN_LIMIT) {
                memset(frame_data, 0x00, BOOK_CAPTURE_BUFFER);
                memset(&capture, 0x00, sizeof(capture));
                capture.frame.data = frame_data;
                capture.frame.capacity = BOOK_CAPTURE_BUFFER;

                status = nosix_read_timeout(
                        session->prog_data->nosix_net,
                        &capture,
                        timeout_ms
                );
                session->prog_data->nosix_status = status;
                timeout_ms = 0;

                if (status == NOSIX_TIMEOUT || status == NOSIX_ERR_STATE) break;
                if (status != NOSIX_OK && status != NOSIX_TRUNCATED) {
                        free(frame_data);
                        return ABNORMAL;
                }

                reads++;
                {
                        int DIRECTION = 0;
                        int8_t TRANSPORT_MATCH = books_capture_matches_transport(
                                session,
                                &capture,
                                &DIRECTION
                        );
                        int SUPPORTING_DIRECTION = 0;
                        int8_t SUPPORTING_MATCH = books_capture_matches_supporting(
                                session,
                                &capture,
                                &SUPPORTING_DIRECTION
                        );

                        // @@ Global runtime counters are wire-oriented. Book-local
                        // tx_bytes/rx_bytes remain application payload counters.
                        if (TRANSPORT_MATCH == ISTRUE || SUPPORTING_MATCH == ISTRUE) {
                                uint64_t WIRE_BYTES = capture.wire_length
                                        ? capture.wire_length
                                        : capture.frame.length;
                                int ACCOUNT_DIRECTION = TRANSPORT_MATCH == ISTRUE
                                        ? DIRECTION
                                        : SUPPORTING_DIRECTION;

                                if (ACCOUNT_DIRECTION > 0) {
                                        session->prog_data->total_tx_bytes += WIRE_BYTES;
                                } else if (ACCOUNT_DIRECTION < 0) {
                                        session->prog_data->total_rx_bytes += WIRE_BYTES;
                                }
                        }

                        if (
                                (TRANSPORT_MATCH == ISTRUE || SUPPORTING_MATCH == ISTRUE)
                                && books_pcap_capture(session, &capture) != NORMAL
                        ) {
                                free(frame_data);
                                if (session->termination == BOOK_TERM_NONE) {
                                        session->termination = BOOK_TERM_CAPTURE_SETUP_ERROR;
                                        session->capture_active = ISFALSE;
                                        books_audit_action(session, "ERROR", "reason=pcap_packet");
                                }
                                return ABNORMAL;
                        }
                }
        }

        free(frame_data);

        if (reads >= BOOK_CAPTURE_DRAIN_LIMIT) {
                session->termination = BOOK_TERM_LIMIT_ERROR;
                session->capture_active = ISFALSE;
                books_audit_action(session, "ERROR", "reason=capture_drain_limit");
                return ABNORMAL;
        }

        return NORMAL;
}

// @@ Open or reuse one target-pinned TCP stack
nosix_status_t books_session_open_tcp(
        BOOK_SESSION * session,
        uint16_t port,
        int32_t timeout_ms
) {
        nosix_address_t destination;
        nosix_status_t status;

        if (
                !session
                || !session->prog_data
                || !session->prog_data->nosix_net
                || port == 0
        ) return NOSIX_ERR_ARGUMENT;
        if (books_session_network_allowed(session) != ISTRUE) return NOSIX_ERR_STATE;
        if (session->datagram) return NOSIX_ERR_STATE;

        if (session->stream) {
                return (
                        session->transport == BOOK_TRANSPORT_TCP
                        && session->destination_port == port
                ) ? NOSIX_OK : NOSIX_ERR_STATE;
        }

        status = books_session_destination(session, &destination);
        if (status != NOSIX_OK) return status;

        session->address_family = destination.family;
        session->transport = BOOK_TRANSPORT_TCP;
        session->destination_port = port;
        session->local_port = 0;

        books_session_note_network_action(session);
        status = nosix_stream_open(
                session->prog_data->nosix_net,
                &session->stream,
                &destination,
                port,
                timeout_ms
        );
        session->prog_data->nosix_status = status;

        // Preserve SYN/SYN-ACK/RST plus target-attributable setup/error evidence.
        if (books_session_capture_drain(session, 10) != NORMAL) {
                if (session->stream) nosix_stream_close(&session->stream);
                return session->termination == BOOK_TERM_LIMIT_ERROR
                        ? NOSIX_TRUNCATED
                        : NOSIX_ERR_SYSTEM;
        }

        return status;
}

// @@ Open or reuse one fixed-peer UDP stack; opening itself emits no datagram
nosix_status_t books_session_open_udp(
        BOOK_SESSION * session,
        uint16_t port,
        int32_t timeout_ms
) {
        nosix_address_t destination;
        nosix_status_t status;

        if (
                !session
                || !session->prog_data
                || !session->prog_data->nosix_net
                || port == 0
        ) return NOSIX_ERR_ARGUMENT;
        if (books_session_network_allowed(session) != ISTRUE) return NOSIX_ERR_STATE;
        if (session->stream) return NOSIX_ERR_STATE;

        if (session->datagram) {
                return (
                        session->transport == BOOK_TRANSPORT_UDP
                        && session->destination_port == port
                ) ? NOSIX_OK : NOSIX_ERR_STATE;
        }

        status = books_session_destination(session, &destination);
        if (status != NOSIX_OK) return status;

        session->address_family = destination.family;
        session->transport = BOOK_TRANSPORT_UDP;
        session->destination_port = port;
        session->local_port = 0;

        status = nosix_datagram_open(
                session->prog_data->nosix_net,
                &session->datagram,
                &destination,
                port,
                timeout_ms
        );
        session->prog_data->nosix_status = status;
        return status;
}

// @@ TCP-only stream write used directly by the TLS overlay
nosix_status_t books_session_stream_write(
        BOOK_SESSION * session,
        const uint8_t * data,
        size_t length,
        size_t * written,
        int32_t timeout_ms
) {
        nosix_status_t status;
        size_t local_written = 0;

        if (written) *written = 0;
        if (!session || (!data && length > 0)) return NOSIX_ERR_ARGUMENT;
        if (
                books_session_network_allowed(session) != ISTRUE
                || !session->stream
                || session->transport != BOOK_TRANSPORT_TCP
        ) return NOSIX_ERR_STATE;

        books_session_note_network_action(session);
        status = nosix_stream_write(
                session->stream,
                data,
                length,
                &local_written,
                timeout_ms
        );

        session->tx_bytes += local_written;
        session->prog_data->nosix_status = status;
        if (written) *written = local_written;

        if (books_session_capture_drain(session, 10) != NORMAL) {
                return session->termination == BOOK_TERM_LIMIT_ERROR
                        ? NOSIX_TRUNCATED
                        : NOSIX_ERR_SYSTEM;
        }

        return status;
}

// @@ TCP-only stream read used directly by the TLS overlay
nosix_status_t books_session_stream_read(
        BOOK_SESSION * session,
        uint8_t * data,
        size_t capacity,
        size_t * received,
        int32_t timeout_ms
) {
        nosix_status_t status;
        size_t local_received = 0;

        if (received) *received = 0;
        if (!session || !data || capacity == 0) return NOSIX_ERR_ARGUMENT;
        if (
                books_session_network_allowed(session) != ISTRUE
                || !session->stream
                || session->transport != BOOK_TRANSPORT_TCP
        ) return NOSIX_ERR_STATE;

        books_session_note_network_action(session);
        status = nosix_stream_read(
                session->stream,
                data,
                capacity,
                &local_received,
                timeout_ms
        );

        session->rx_bytes += local_received;
        session->prog_data->nosix_status = status;
        if (received) *received = local_received;

        if (books_session_capture_drain(session, 10) != NORMAL) {
                return session->termination == BOOK_TERM_LIMIT_ERROR
                        ? NOSIX_TRUNCATED
                        : NOSIX_ERR_SYSTEM;
        }

        return status;
}

// @@ Top-of-stack raw payload write; UDP preserves one-call/one-datagram semantics
nosix_status_t books_session_payload_write(
        BOOK_SESSION * session,
        const uint8_t * data,
        size_t length,
        size_t * written,
        int32_t timeout_ms
) {
        nosix_status_t status;
        size_t local_written = 0;

        if (written) *written = 0;
        if (!session || (!data && length > 0)) return NOSIX_ERR_ARGUMENT;

        if (session->transport == BOOK_TRANSPORT_TCP) {
                return books_session_stream_write(
                        session,
                        data,
                        length,
                        written,
                        timeout_ms
                );
        }

        if (
                session->transport != BOOK_TRANSPORT_UDP
                || !session->datagram
                || books_session_network_allowed(session) != ISTRUE
        ) return NOSIX_ERR_STATE;

        books_session_note_network_action(session);
        status = nosix_datagram_write(
                session->datagram,
                data,
                length,
                &local_written,
                timeout_ms
        );

        session->tx_bytes += local_written;
        session->prog_data->nosix_status = status;
        if (written) *written = local_written;

        if (books_session_capture_drain(session, 10) != NORMAL) {
                return session->termination == BOOK_TERM_LIMIT_ERROR
                        ? NOSIX_TRUNCATED
                        : NOSIX_ERR_SYSTEM;
        }

        return status;
}

// @@ Top-of-stack raw payload read; UDP consumes exactly one datagram
nosix_status_t books_session_payload_read(
        BOOK_SESSION * session,
        uint8_t * data,
        size_t capacity,
        size_t * received,
        int32_t timeout_ms
) {
        nosix_status_t status;
        size_t local_received = 0;

        if (received) *received = 0;
        if (!session || !data || capacity == 0) return NOSIX_ERR_ARGUMENT;

        if (session->transport == BOOK_TRANSPORT_TCP) {
                return books_session_stream_read(
                        session,
                        data,
                        capacity,
                        received,
                        timeout_ms
                );
        }

        if (
                session->transport != BOOK_TRANSPORT_UDP
                || !session->datagram
                || books_session_network_allowed(session) != ISTRUE
        ) return NOSIX_ERR_STATE;

        books_session_note_network_action(session);
        status = nosix_datagram_read(
                session->datagram,
                data,
                capacity,
                &local_received,
                timeout_ms
        );

        if (local_received > capacity) local_received = capacity;
        session->rx_bytes += local_received;
        session->prog_data->nosix_status = status;
        if (received) *received = local_received;

        if (books_session_capture_drain(session, 10) != NORMAL) {
                return session->termination == BOOK_TERM_LIMIT_ERROR
                        ? NOSIX_TRUNCATED
                        : NOSIX_ERR_SYSTEM;
        }

        return status;
}

// @@ Finalize evidence on complete, bail, runtime/transport/TLS failure, or limit
void books_session_finalize(BOOK_SESSION * session) {
        char detail[256];

        if (!session) return;
        if (session->state == BOOK_SESSION_FINALIZED) return;

        if (session->stream) {
                nosix_stream_close(&session->stream);
                if (session->termination != BOOK_TERM_LIMIT_ERROR) {
                        (void)books_session_capture_drain(session, 50);
                }
        }

        if (session->datagram) {
                if (session->termination != BOOK_TERM_LIMIT_ERROR) {
                        (void)books_session_capture_drain(session, 50);
                }
                nosix_datagram_close(&session->datagram);
        }

        session->state = BOOK_SESSION_CLOSING;

        if (session->pcap_fd >= 0) {
                (void)fsync(session->pcap_fd);
                close(session->pcap_fd);
                session->pcap_fd = -1;
        }

        session->capture_active = ISFALSE;
        session->ended_ns = books_now_ns();
        session->state = BOOK_SESSION_FINALIZED;
        if (session->termination == BOOK_TERM_NONE) session->termination = BOOK_TERM_INTERRUPTED;

        memset(detail, 0x00, sizeof(detail));
        snprintf(
                detail,
                sizeof(detail),
                "status=%s transport=%s remote_port=%u local_port=%u actions=%" PRIu64
                " tx=%" PRIu64 " rx=%" PRIu64 " pcap_packets=%" PRIu64 " pcap_bytes=%" PRIu64,
                books_termination_name(session->termination),
                session->transport == BOOK_TRANSPORT_TCP ? "tcp" :
                        (session->transport == BOOK_TRANSPORT_UDP ? "udp" : "none"),
                (unsigned int)session->destination_port,
                (unsigned int)session->local_port,
                session->network_actions,
                session->tx_bytes,
                session->rx_bytes,
                session->pcap_packets,
                session->pcap_bytes
        );
        books_audit_action(session, "END", detail);
}
