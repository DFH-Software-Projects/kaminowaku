// Copyright 2026 Jamison A. Drapeau
#ifndef __DATA__H
#define __DATA__H

#include <nosix.h>

// Version
#define VERSION "0.1.1"

// System Directories
#define KAMI_SYSTEM_SHARE_DIR           "/usr/local/share/kaminowaku"
#define KAMI_SYSTEM_PROFILES_DIR        "/usr/local/share/kaminowaku/profiles/"
#define KAMI_SYSTEM_TOOLS_DIR           "/usr/local/share/kaminowaku/tools/"
#define KAMI_SYSTEM_BOOKS_DIR           "/usr/local/share/kaminowaku/books/"
#define KAMI_SYSTEM_BOOKS_MAIN_DIR      "/usr/local/share/kaminowaku/books/main/"
#define KAMI_SYSTEM_BOOKS_MODULES_DIR   "/usr/local/share/kaminowaku/books/modules/"
#define KAMI_SYSTEM_DEFAULT_PROFILE     "/usr/local/share/kaminowaku/profiles/default.ini"

// User Directories
#define KAMI_USER_ROOT_DIR              ".kaminowaku/"
#define KAMI_USER_LOGS_DIR              "logs/"
#define KAMI_USER_PROFILES_DIR          "profiles/"
#define KAMI_USER_PROJECTS_DIR          "projects/"
#define KAMI_USER_TOOLS_DIR             "tools/"
#define KAMI_USER_BOOKS_DIR             "books/"
#define KAMI_USER_BOOK_MODULES_DIR      "books/modules/"
#define KAMI_USER_DEFAULT_PROFILE       "profiles/default.ini"

// TUI Hard Coded Params
#define KUI_MIN_ROWS           24
#define KUI_MAX_SCROLL_LINES   16384
#define KUI_MAX_SCROLL_COLS    512

// True/False/Init Readability
#define GLOBAL_RESET    -1
#define FD_RESET        -1
#define RESET            0
#define ISFALSE          0
#define ISTRUE           1

// Better readability for some functions
#define MATCH     0
#define NORMAL    0
#define ABNORMAL -1

// Modes for target functions
#define MODE_IDENTIFIER 0
#define MODE_TID        1

// Modes for banner
#define RENDER_NORMAL    0
#define RENDER_SCROLLING 1

// Command history
#define CMD_HISTORY_LIMIT      25

// Token initializations
#define TOKENS_INITIAL_CAPACITY 2
#define TOKENS_INITIAL_SIZE     0

// Port statuses
#define PORT_NOT_SCANNED        0
#define PORT_OPEN               1
#define PORT_CLOSED            -1

// @@ Compact scan result bits
#define SCAN_RESULT_RAN         (1U << 0)
#define SCAN_RESULT_REPLY       (1U << 1)
#define SCAN_RESULT_TIMEOUT     (1U << 2)
#define SCAN_RESULT_ERROR       (1U << 3)
#define SCAN_RESULT_ROUTE       (1U << 4)
#define SCAN_RESULT_NEIGHBOR    (1U << 5)
#define TARGET_SCAN_DATA_VERSION 1U

// Maximum Maps
#define MAX_MAPS                1000

// INI/Profile block sizes
#define PROFILE_NAME_BLOCK      64
#define PROFILE_SCOPE_BLOCK     32
#define IFACE_BLOCK             64
#define DOMAIN_BLOCK            256

// Define standard data widths
#define        INTERNET  4294967294             // @@ Max number of IPv4 Addresses, no reason to scan this many targets.
#define        MAX_PATH       65536 
#define       MAX_PORTS       65536
#define       OUT_BLOCK       65536
#define     INPUT_BLOCK        1024
#define       MAX_BLOCK        4096
#define    SUPSUP_BLOCK         128
#define     SUPER_BLOCK          64
#define    DOUBLE_BLOCK          32
#define       TID_BLOCK          17
#define           BLOCK          16

// Target specific data widths
#define  URL_BLOCK 4096
#define NOTE_BLOCK  128
#define   IP_BLOCK   64
#define IPV6_BLOCK   64
#define LAST_BLOCK   64
#define  MAC_BLOCK   64
#define IPV4_BLOCK   32

// Define Colors
#define ANSI_COLOR_RED          "\x1b[31m"
#define ANSI_COLOR_GREEN        "\x1b[32m"
#define ANSI_COLOR_YELLOW       "\x1b[33m"
#define ANSI_COLOR_BLUE         "\x1b[34m"
#define ANSI_COLOR_MAGENTA      "\x1b[35m"
#define ANSI_COLOR_CYAN         "\x1b[36m"
#define RGB_COLOR_SOFT_GREY     "\x1b[38;2;150;150;150m"
#define RGB_COLOR_ORANGE        "\x1b[38;2;255;165;0m"
#define RGB_COLOR_ORANGE_BOLD   "\x1b[1m\x1b[38;2;255;165;0m"
#define RGB_COLOR_DARK_MAGENTA  "\x1b[38;2;48;0;48m"   // #300030 (Blood Amethyst)
#define RGB_COLOR_BRIGHT_YELLOW "\x1b[38;2;255;255;85m"
#define RGB_COLOR_BRIGHT_GREEN  "\x1b[38;2;0;255;0m"
#define RGB_COLOR_PINKISH_RED   "\x1b[38;2;255;85;85m"
#define RGB_COLOR_RICH_RED      "\x1b[38;2;200;0;0m"
#define ANSI_COLOR_RESET        "\x1b[0m"

//  printf(ANSI_COLOR_RED     "This text is RED!"     ANSI_COLOR_RESET "\n");
//  printf(ANSI_COLOR_GREEN   "This text is GREEN!"   ANSI_COLOR_RESET "\n");
//  printf(ANSI_COLOR_YELLOW  "This text is YELLOW!"  ANSI_COLOR_RESET "\n");
//  printf(ANSI_COLOR_BLUE    "This text is BLUE!"    ANSI_COLOR_RESET "\n");
//  printf(ANSI_COLOR_MAGENTA "This text is MAGENTA!" ANSI_COLOR_RESET "\n");
//  printf(ANSI_COLOR_CYAN    "This text is CYAN!"    ANSI_COLOR_RESET "\n");

// Notices
#define NOTICE_INFO          "[" RGB_COLOR_SOFT_GREY     "i" ANSI_COLOR_RESET "] "
#define NOTICE_SUCCESS       "[" RGB_COLOR_BRIGHT_GREEN  "+" ANSI_COLOR_RESET "] "
#define NOTICE_WARNING       "[" RGB_COLOR_BRIGHT_YELLOW "-" ANSI_COLOR_RESET "] "
#define NOTICE_NOPE          "[" RGB_COLOR_PINKISH_RED   "x" ANSI_COLOR_RESET "] " 
#define NOTICE_ERROR         "[" RGB_COLOR_RICH_RED      "!" ANSI_COLOR_RESET "] "
#define NOTICE_TRANSMISSION  "[" ANSI_COLOR_CYAN         "←" ANSI_COLOR_RESET "] "
#define NOTICE_RECIEVE       "[" ANSI_COLOR_CYAN         "→" ANSI_COLOR_RESET "] "
#define NOTICE_CHURNING                                                    "< ☢  "


// @@ Kui scrollback lines
typedef struct KUI_SCROLLBACK {
        char     lines[KUI_MAX_SCROLL_LINES][KUI_MAX_SCROLL_COLS];
        unsigned line_count;    // number of valid lines in buffer (<= KUI_MAX_SCROLL_LINES)
        unsigned view_offset;   // 0 = follow tail; >0 = scrollback (lines above tail)
        unsigned head;          // index of oldest line in 'lines' ring buffer
} KUI_SCROLLBACK;

// @@ f-type definitions
// S = State tracking
// C = Command tracking
typedef enum {              
        S_STARTUP               =       0,
        S_DEFAULT               =       1,
        C_HELP                  =       2,
        C_PWD                   =       3,
        C_NEW                   =       4,
        C_NEW_CONTEXTUAL        =      -4,
        C_UNLOAD                =       5,
        C_LOAD                  =       6,
        C_LOAD_CONTEXTUAL       =      -6,
        C_TARGETS               =       7,
        C_TARGETS_CONTEXTUAL    =      -7,
        C_PROFILE               =       8,
        C_PROFILE_CONTEXTUAL    =      -8,
        C_INTERFACES            =       9,
        C_PING                  =      10,
        C_RESOLVE               =      11,
        C_SCAN                  =      12,
        C_BOOK                  =      13,
        C_TOOL                  =      14,
        C_TOOL_CONTEXTUAL       =     -14,
        C_COMMAND_NOT_FOUND     =   30000,
        C_DEBUG                 =   99999
} f_type_t;

// @@ Macro for smaller int sizes, CLANG and GCC Supported
typedef __INT8_TYPE__ int8_t;
typedef __UINT64_TYPE__ uint64_t;

// @@ Type definition of POSIX portable size_t equiv
typedef unsigned long c_size_t;

// @@ Scan metadata occupies the legacy 64-byte LAST_SCAN_TIME slot exactly
typedef struct TARGET_SCAN_DATA {
        uint64_t        LAST_SCAN_NS;
        uint64_t        ICMPV4_SCAN_NS;
        uint8_t         SCAN_VERSION;
        uint8_t         ICMPV4;
        unsigned char   RESERVED[46];
} TARGET_SCAN_DATA;

// @@ Target Datatype
typedef struct TARGET {
        // @@ Primary target identifiers
        unsigned char URL[URL_BLOCK];
        unsigned char IPV4[IPV4_BLOCK];
        unsigned char IPV6[IPV6_BLOCK];
        unsigned char MAC[MAC_BLOCK];

        // @@ Internal target data
        int8_t PORTS[MAX_PORTS];
        union {
                unsigned char LAST_SCAN_TIME[LAST_BLOCK];       // Legacy alias / zero initialization
                TARGET_SCAN_DATA SCAN;
        };
        unsigned char NOTE[NOTE_BLOCK];
} TARGET;

// @@ Primary Master Index
typedef struct FLOWER {
        unsigned char   TID[TID_BLOCK];
        int             FD;
        uint64_t        LAST_MAP;
        c_size_t        MAP_SIZE;
        void *          MAP_ADDRESS;
        TARGET *        PETAL;
        struct FLOWER * NEXT;
} FLOWER;

// @@ Global Profile Enumerations
typedef enum {
        TXG_UNSET               =       -1, // REPORT ERROR
        TXG_RANDOM              = 0,
        TXG_STATIC              = 1,
        TXG_INCREMENT           = 2
} tx_generation_t;

typedef enum {
        RXS_UNSET               =       -1, // REPORT ERROR
        RXS_TRANSACTION         = 0,
        RXS_STREAM              = 1
} rx_state_t;

typedef enum {
        MATCH_UNSET             =       -1, // REPORT ERROR
        MATCH_STRICT            = 0,
        MATCH_LOOSE             = 1
} match_policy_t;

typedef enum {
        CHECKSUM_UNSET          =       -1, // REPORT ERROR
        CHECKSUM_AUTO           = 0,
        CHECKSUM_MANUAL         = 1
} checksum_mode_t;

// @@ Global Profile
typedef struct GLOBAL_PROFILE {

        // Profile Metadata
        unsigned char           profile_name[PROFILE_NAME_BLOCK];
        unsigned char           scope[PROFILE_SCOPE_BLOCK];
        int                     profile_schema_version;

        // Packet Transmission Policy
        checksum_mode_t         tx_checksum_mode;
        tx_generation_t         tx_ipv4_id_generation;
        tx_generation_t         tx_tcp_src_port_generation;
        unsigned char           tx_interface[IFACE_BLOCK];
        unsigned char           tx_source_ip[IP_BLOCK];
        unsigned char           tx_gateway[IP_BLOCK];
        int8_t                  tx_strict;
        int                     tx_ipv6_hop_limit;
        int                     tx_ipv4_ttl;

        // Packet Retrieval/Recieve Policy
        match_policy_t          rx_match_policy;
        rx_state_t              rx_state;
        unsigned char           rx_interface[IFACE_BLOCK];
        int8_t                  rx_promiscuous;
        int                     rx_timeout_ms;
        int                     rx_snaplength;
        int8_t                  rx_dedupe;

        // DNS Resolution Policy
        unsigned char           dns_server1[IPV4_BLOCK];
        unsigned char           dns_server2[IPV4_BLOCK];
        unsigned char           dns_search_domain[DOMAIN_BLOCK];
        int                     dns_timeout_ms;
        int                     dns_retries;

        // Resource Limits
        unsigned int            limit_max_pending_transactions;
        unsigned int            limit_max_capture_bytes_per_tx;
        unsigned int            limit_max_dns_queries_pending;
        unsigned int            limit_max_targets_per_batch;
        unsigned int            limit_max_scan_workers;
        unsigned int            limit_max_tx_out_rate_pps;
        unsigned int            limit_max_rx_in_rate_pps;
        uint64_t                limit_max_capture_bytes_total;

} GLOBAL_PROFILE;

// @@ Define persistent program data
typedef struct {
        // Debug switch
        int8_t                  debug_flag;
        // Prompt String
        unsigned char           prompt[DOUBLE_BLOCK];
        // Depth Tracking
        f_type_t                f_type;
        // Working Directory
        unsigned char           wd[MAX_BLOCK];
        // Log file pointer
        void *                  log;

        // TUI
        unsigned int            term_rows;
        unsigned int            term_cols;
        KUI_SCROLLBACK          kui_scrollback;
        int8_t                  log_frame_start;
        int8_t                  render_mode;

        // I/O Data
        unsigned char           cmd_output[OUT_BLOCK];
        unsigned char           cmd_input[INPUT_BLOCK];
        unsigned char           cmd_last[INPUT_BLOCK];
        unsigned char           cmd_history[CMD_HISTORY_LIMIT + 1][INPUT_BLOCK]; // 0 = current draft, 1-10 = newest to oldest
        unsigned int            cmd_history_count;                               // valid entries stored in indexes 1-10

        unsigned char **        cmd_tokens;   
        c_size_t                cmd_tokens_count;
        
        // Global Profile
        GLOBAL_PROFILE          gprof;

        // NOSIX Runtime
        nosix_t *               nosix_net;                              // Active project network runtime; NULL means network offline
        nosix_status_t          nosix_status;                           // Last NOSIX lifecycle status returned to Kaminowaku

        // @@ State Variables
        //----------------------------------------------------------------------------------------------------
        unsigned char           active_project[MAX_BLOCK];              // acceptable size, should never exceed max project name length of 64 - 4
        int8_t                  active_project_has_been_scanned;        // True or False
        uint64_t                active_project_last_scan_ns;            // Most recent scan in the active project
        int8_t                  active_project_has_targets;             // True or False
        uint64_t                active_project_target_count;            // Big num.
        FLOWER *                active_project_flower;                  // Linked list for targets
        FLOWER *                active_project_active_target;           // For contextual operations on targets
        int8_t                  active_project_active_target_context;   // Helper flag
        char *                  active_project_active_target_directory; // Path tracking for target context
        int8_t                  unload_from_new;                        // "Jump Flag" Kinda kludge but works, True or False
        int8_t                  new_from_unload;                        // "Print logic"
        int8_t                  unload_from_load;                       // Kludgy mc kludgins
        int8_t                  load_from_unload;                       // Kludgy mc kludgins
        int8_t                  help_caller_state_manager;              // Condenses help menu, will return to state after help.
        
        // Runtime wire accounting
        uint64_t                total_tx_bytes;                         // Total bytes transmitted during this runtime
        uint64_t                total_rx_bytes;                         // Total bytes received during this runtime
        //----------------------------------------------------------------------------------------------------
        
} _carry_forward;
#endif
