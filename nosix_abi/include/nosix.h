// Copyright 2026 Jamison A. Drapeau
// nosix.h - Public NOSIX network I/O API

#ifndef NOSIX_H
#define NOSIX_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif


/*
 * Public ABI version.
 *
 * ABI-major changes require a new shared-library SONAME.
 *
 * Example:
 *
 *      ABI 1 -> libnosix.so.1
 */
#define NOSIX_ABI_VERSION_MAJOR 1U
#define NOSIX_ABI_VERSION_MINOR 4U


/*
 * Maximum storage reserved for an interface name,
 * including the terminating NUL byte.
 *
 * This is deliberately independent of platform-specific
 * constants such as IFNAMSIZ.
 */
#define NOSIX_IFACE_NAME_MAX 64U


/*
 * Common IP protocol numbers accepted by
 * nosix_tx_packet_t.ip_protocol.
 *
 * Other protocol numbers may be supplied by the caller.
 */
#define NOSIX_IPPROTO_ICMP    1U
#define NOSIX_IPPROTO_TCP     6U
#define NOSIX_IPPROTO_UDP    17U
#define NOSIX_IPPROTO_ICMPV6 58U


/*
 * Zero indicates normal completion.
 *
 * Positive values indicate non-fatal conditions where the
 * operation completed sufficiently to return useful data.
 *
 * Negative values indicate operation failure.
 *
 * NOSIX_ERR_ADDRESS indicates that a caller-supplied or
 * selected network address is invalid or incompatible with
 * the requested operation.
 *
 * NOSIX_ERR_ROUTE indicates that NOSIX could not determine
 * or honor a viable route/interface for the destination.
 *
 * NOSIX_ERR_NEIGHBOR indicates that the route and next hop
 * were determined, but required link-layer neighbor
 * resolution did not produce a usable address.
 */
typedef enum nosix_status {
        NOSIX_OK                  =  0,

        NOSIX_TIMEOUT             =  1,
        NOSIX_TRUNCATED           =  2,
        NOSIX_EOF                 =  3,

        NOSIX_ERR_ARGUMENT        = -1,
        NOSIX_ERR_STATE           = -2,
        NOSIX_ERR_MEMORY          = -3,
        NOSIX_ERR_SYSTEM          = -4,
        NOSIX_ERR_ADDRESS         = -5,
        NOSIX_ERR_FRAME_TOO_LARGE = -6,
        NOSIX_ERR_UNSUPPORTED     = -7,
        NOSIX_ERR_ROUTE           = -8,
        NOSIX_ERR_NEIGHBOR        = -9,
        NOSIX_ERR_CONNECTION      = -10
} nosix_status_t;


/*
 * Surface used by the most recent successful managed
 * transmission.
 *
 * NOSIX_TX_SURFACE_ETHERNET means NOSIX transmitted a
 * complete Ethernet frame through the platform L2 backend.
 *
 * NOSIX_TX_SURFACE_IPV4_LOCAL means the destination belongs
 * to the local host and NOSIX transmitted a complete IPv4
 * packet through the local raw-IP path. Neighbor resolution
 * is not performed for this surface.
 */
typedef enum nosix_tx_surface {
        NOSIX_TX_SURFACE_NONE       = 0,
        NOSIX_TX_SURFACE_ETHERNET   = 1,
        NOSIX_TX_SURFACE_IPV4_LOCAL = 2
} nosix_tx_surface_t;


/*
 * Address bytes are stored in network byte order.
 *
 * NOSIX_ADDRESS_NONE represents an unset or
 * automatically derived address.
 *
 * These values are NOSIX ABI values and are not
 * platform AF_* constants.
 */
typedef enum nosix_address_family {
        NOSIX_ADDRESS_NONE = 0,
        NOSIX_ADDRESS_IPV4 = 4,
        NOSIX_ADDRESS_IPV6 = 6
} nosix_address_family_t;


/*
 * Platform-independent IP address storage.
 */
typedef struct nosix_address {
        nosix_address_family_t family;

        union {
                uint8_t ipv4[4];
                uint8_t ipv6[16];
        } bytes;
} nosix_address_t;


/*
 * AUTO:
 *
 *      NOSIX finalizes supported upper-layer checksums
 *      after constructing the L3 header.
 *
 *      Supported protocols may include:
 *
 *              ICMP
 *              ICMPv6
 *              TCP
 *              UDP
 *
 *      If automatic checksum generation is requested for
 *      an unsupported protocol, nosix_write() returns
 *      NOSIX_ERR_UNSUPPORTED.
 *
 *
 * MANUAL:
 *
 *      NOSIX preserves the checksum supplied in the
 *      caller's upper-layer bytes.
 *
 *      The IPv4 header checksum remains owned by NOSIX
 *      because NOSIX constructs the IPv4 header.
 *
 *
 * OFFLOAD:
 *
 *      Request platform or NIC checksum offload.
 *
 *      A backend may reject this mode with
 *      NOSIX_ERR_UNSUPPORTED.
 *
 *
 * This setting applies only to nosix_write().
 *
 * nosix_injection() never modifies caller-supplied
 * checksums.
 */
typedef enum nosix_checksum_mode {
        NOSIX_CHECKSUM_AUTO    = 0,
        NOSIX_CHECKSUM_MANUAL  = 1,
        NOSIX_CHECKSUM_OFFLOAD = 2
} nosix_checksum_mode_t;


/*
 * RANDOM:
 *
 *      Generate a new IPv4 identification value for
 *      every transmitted IPv4 packet.
 *
 *
 * STATIC:
 *
 *      Use nosix_config_t.ipv4_id_value for every
 *      transmitted IPv4 packet.
 *
 *
 * INCREMENT:
 *
 *      Begin with nosix_config_t.ipv4_id_value and
 *      increment the value for each IPv4 packet.
 *
 *
 * This setting applies only to IPv4 traffic transmitted
 * through nosix_write().
 *
 * It has no effect on IPv6 transmission or
 * nosix_injection().
 */
typedef enum nosix_ipv4_id_mode {
        NOSIX_IPV4_ID_RANDOM    = 0,
        NOSIX_IPV4_ID_STATIC    = 1,
        NOSIX_IPV4_ID_INCREMENT = 2
} nosix_ipv4_id_mode_t;


/*
 * nosix_config_t flags
 */
#define NOSIX_CONFIG_RX_PROMISCUOUS  (1U << 0)
#define NOSIX_CONFIG_TX_STRICT       (1U << 1)
#define NOSIX_CONFIG_TX_SOURCE_AUTO  (1U << 2)
#define NOSIX_CONFIG_TX_GATEWAY_AUTO (1U << 3)


/*
 * Public runtime configuration.
 *
 * nosix_init() copies this structure into private runtime
 * storage.
 *
 * The caller may release or reuse the original structure
 * after nosix_init() returns.
 *
 *
 * TX INTERFACE
 * ------------
 *
 * tx_interface identifies the interface used for
 * transmission.
 *
 * Managed traffic transmitted by nosix_write() is
 * constrained to this interface.
 *
 * Raw link-layer traffic transmitted by
 * nosix_injection() is injected directly through this
 * interface.
 *
 *
 * RX INTERFACE
 * ------------
 *
 * rx_interface identifies the interface used for frame
 * capture.
 *
 *
 * SOURCE ADDRESS
 * --------------
 *
 * When NOSIX_CONFIG_TX_SOURCE_AUTO is set:
 *
 *      tx_source_address is ignored.
 *
 *      NOSIX derives an appropriate source address from
 *      the selected interface and routing information.
 *
 * Otherwise:
 *
 *      tx_source_address contains the caller-selected
 *      source address.
 *
 *      NOSIX validates and uses that address according to
 *      the configured transmission policy.
 *
 *
 * GATEWAY / NEXT-HOP POLICY
 * -------------------------
 *
 * When NOSIX_CONFIG_TX_GATEWAY_AUTO is set:
 *
 *      tx_gateway_address is ignored.
 *
 *      NOSIX determines whether the destination is
 *      directly reachable or requires a gateway using
 *      platform routing information.
 *
 * Otherwise:
 *
 *      tx_gateway_address contains the caller-selected
 *      gateway used for destinations requiring routed
 *      transmission.
 *
 * A directly connected destination may itself become the
 * resolved next hop.
 *
 * The configured gateway and the final resolved next hop
 * are therefore related but are not necessarily the same
 * concept.
 *
 *
 * STRICT TRANSMISSION
 * -------------------
 *
 * When NOSIX_CONFIG_TX_STRICT is set:
 *
 *      Explicitly supplied TX configuration must be
 *      honored.
 *
 *      NOSIX must not silently substitute another
 *      interface, source address, or gateway when an
 *      explicit value cannot be used.
 *
 *      Configuration explicitly marked AUTO remains
 *      eligible for automatic selection.
 *
 *
 * RAW INJECTION
 * -------------
 *
 * Routing, source-address, gateway, TTL, hop-limit,
 * checksum, and IPv4 identification policies do not alter
 * frames supplied to nosix_injection().
 *
 * nosix_injection() uses tx_interface but otherwise treats
 * the supplied frame as complete caller-owned wire data.
 */
typedef struct nosix_config {
        char                    tx_interface[
                                        NOSIX_IFACE_NAME_MAX
                                ];

        char                    rx_interface[
                                        NOSIX_IFACE_NAME_MAX
                                ];

        nosix_address_t         tx_source_address;
        nosix_address_t         tx_gateway_address;

        nosix_checksum_mode_t   checksum_mode;
        nosix_ipv4_id_mode_t    ipv4_id_mode;

        uint16_t                ipv4_id_value;

        uint8_t                 ipv4_ttl;
        uint8_t                 ipv6_hop_limit;

        uint32_t                rx_snaplength;
        int32_t                 rx_timeout_ms;

        uint32_t                flags;
} nosix_config_t;


/*
 * One caller-provided upper-layer message for managed
 * network transmission.
 *
 * The caller constructs the complete upper-layer bytes,
 * including headers belonging to that protocol.
 *
 * Examples:
 *
 *      ICMP
 *      ICMPv6
 *      TCP
 *      UDP
 *      custom IP protocols
 *
 *
 * NOSIX then:
 *
 *      1. Validates the destination.
 *
 *      2. Selects or validates the source address.
 *
 *      3. Determines whether the destination is on-link,
 *         routed, or local to this host.
 *
 *      4. For Ethernet delivery, determines the
 *         appropriate next-hop address.
 *
 *      5. Resolves required link-layer addressing when
 *         the selected transport requires it.
 *
 *              IPv4 / Ethernet:
 *                      ARP or platform neighbor state
 *
 *              IPv4 / local host:
 *                      no link-layer neighbor resolution
 *
 *              IPv6 / Ethernet:
 *                      Neighbor Discovery or platform
 *                      neighbor state
 *
 *      6. Builds the IPv4 or IPv6 header.
 *
 *      7. Finalizes supported upper-layer checksums when
 *         configured.
 *
 *      8. Builds the link-layer header when the selected
 *         transport uses one.
 *
 *      9. Transmits exactly one packet/frame through the
 *         selected platform transport.
 *
 *
 * The destination address family determines whether
 * NOSIX constructs IPv4 or IPv6.
 *
 * The caller owns upper_layer and retains ownership after
 * nosix_write() returns.
 *
 * NOSIX must not modify the caller's original buffer.
 */
typedef struct nosix_tx_packet {
        nosix_address_t destination;

        uint8_t         ip_protocol;

        const uint8_t  *upper_layer;
        size_t          upper_layer_length;

        /*
         * Reserved for packet-specific behavior.
         *
         * Must currently be zero.
         */
        uint32_t        flags;
} nosix_tx_packet_t;


/*
 * One caller-constructed complete link-layer frame for
 * direct raw injection.
 *
 * frame points to the first byte of the complete frame.
 *
 * frame_length contains the complete number of bytes to
 * inject.
 *
 * The caller owns frame and retains ownership after
 * nosix_injection() returns.
 *
 * NOSIX must not modify the caller's original buffer.
 *
 * flags is reserved for future injection-specific
 * behavior and must currently be zero.
 */
typedef struct nosix_injection {
        const uint8_t  *frame;
        size_t          frame_length;

        uint32_t        flags;
} nosix_injection_t;


/*
 * Caller-owned frame storage used by nosix_read() and
 * nosix_write_frame().
 *
 * Before either operation:
 *
 *      data points to writable memory.
 *
 *      capacity contains the number of writable bytes
 *      available at data.
 *
 *
 * After nosix_read() or successful nosix_write_frame():
 *
 *      length contains the number of bytes copied into
 *      data.
 *
 * For NOSIX_TX_SURFACE_ETHERNET, nosix_write_frame()
 * returns the complete Ethernet frame.
 *
 * For NOSIX_TX_SURFACE_IPV4_LOCAL, nosix_write_frame()
 * returns the complete IPv4 packet because local delivery
 * has no Ethernet header.
 */
typedef struct nosix_frame {
        uint8_t *data;

        size_t   capacity;
        size_t   length;
} nosix_frame_t;


/*
 * nosix_capture_t flags
 */
#define NOSIX_CAPTURE_TRUNCATED  (1U << 0)
#define NOSIX_CAPTURE_IPV4_LOCAL (1U << 1)


/*
 * One captured packet/frame and its metadata.
 *
 * frame.length is the number of bytes copied into
 * frame.data. wire_length is the original packet/frame
 * length.
 *
 * NOSIX_CAPTURE_IPV4_LOCAL means frame.data begins with an
 * IPv4 header rather than an Ethernet header.
 *
 * timestamp_ns is nanoseconds since the Unix epoch when
 * supplied by the platform backend; zero means unavailable.
 */
typedef struct nosix_capture {
        nosix_frame_t frame;

        size_t        wire_length;

        uint64_t      timestamp_ns;
        uint32_t      interface_index;

        uint32_t      flags;
} nosix_capture_t;


/* Opaque public runtime handle. */
typedef struct nosix_context nosix_t;

/* Opaque established TCP stream handle. */
typedef struct nosix_stream nosix_stream_t;


nosix_status_t nosix_init(
        nosix_t **net,
        const nosix_config_t *config
);


nosix_status_t nosix_reopen(
        nosix_t **net,
        const nosix_config_t *config
);


/* Managed network transmission. */
nosix_status_t nosix_write(
        nosix_t *net,
        const nosix_tx_packet_t *packet,
        size_t *frame_length
);


/* Managed transmission with exact completed-packet/frame copy-out. */
nosix_status_t nosix_write_frame(
        nosix_t *net,
        const nosix_tx_packet_t *packet,
        nosix_frame_t *frame,
        size_t *frame_length
);


/*
 * Return the transport surface used by the most recent
 * successful managed transmission.
 */
nosix_tx_surface_t nosix_last_tx_surface(
        const nosix_t *net
);


/* Raw caller-owned link-layer frame injection. */
nosix_status_t nosix_injection(
        nosix_t *net,
        const nosix_injection_t *injection
);


/* Return exactly one captured packet/frame. */
nosix_status_t nosix_read(
        nosix_t *net,
        nosix_capture_t *capture
);


/*
 * Discard capture state accumulated before a new bounded
 * transaction.
 *
 * This operation is intentionally explicit: nosix_write()
 * and nosix_write_frame() never discard captured traffic on
 * their own because callers may be using NOSIX for stream
 * capture.
 *
 * For AUTO-interface runtimes, reset also returns interface
 * selection to its lazy AUTO state so the next managed write
 * performs a fresh route lookup before opening backend
 * handles.
 */
nosix_status_t nosix_capture_reset(
        nosix_t *net
);


/*
 * Open one kernel-managed TCP stream through the NOSIX ABI.
 *
 * Packet-oriented nosix_write()/nosix_read() remain the low-level path.
 * Stream operations exist for application-layer transactions that require
 * a real established TCP session, such as banner collection and safe
 * service probes. Explicit configured source addresses are honored.
 */
nosix_status_t nosix_stream_open(
        nosix_t *net,
        nosix_stream_t **stream,
        const nosix_address_t *destination,
        uint16_t port,
        int32_t timeout_ms
);

nosix_status_t nosix_stream_write(
        nosix_stream_t *stream,
        const uint8_t *data,
        size_t length,
        size_t *written,
        int32_t timeout_ms
);

nosix_status_t nosix_stream_read(
        nosix_stream_t *stream,
        uint8_t *data,
        size_t capacity,
        size_t *received,
        int32_t timeout_ms
);

nosix_status_t nosix_stream_close(
        nosix_stream_t **stream
);


/* Close and release the runtime context. */
nosix_status_t nosix_close(
        nosix_t **net
);


#ifdef __cplusplus
}
#endif

#endif
