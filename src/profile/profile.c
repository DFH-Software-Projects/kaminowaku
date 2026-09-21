// Copyright 2026 Jamison A. Drapeau
//======================================================
// Mostly AI Generated, developer validated and modified
//======================================================
#define _POSIX_C_SOURCE 200809L
#include "profile.h"
#include "gprofile.h"
#include "projects.h"
#include "kui.h"
#include <dirent.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

typedef struct {
        const char *TITLE;
        int8_t OPENED;
        int8_t *PRINTED_ANY;
} PROFILE_DISPLAY_SECTION;

// @@ Match filenames accepted by the lower-level profile path validator.
static int profile_supported_filename(const char * FILENAME) {
        c_size_t LENGTH;
        c_size_t BASE_LENGTH;
        c_size_t INDEX;

        if (!FILENAME) {
                return ABNORMAL;
        }

        LENGTH = strlen(FILENAME);

        if (
                LENGTH <= 4
                || LENGTH >= PROFILE_NAME_BLOCK + 4
                || strcmp(FILENAME + LENGTH - 4, ".ini") != MATCH
        ) {
                return ABNORMAL;
        }

        BASE_LENGTH = LENGTH - 4;

        for (INDEX = 0; INDEX < BASE_LENGTH; INDEX++) {
                unsigned char CHARACTER = (unsigned char)FILENAME[INDEX];

                if (
                        !(
                                (CHARACTER >= 'A' && CHARACTER <= 'Z')
                                || (CHARACTER >= 'a' && CHARACTER <= 'z')
                                || (CHARACTER >= '0' && CHARACTER <= '9')
                        )
                        && CHARACTER != '_'
                        && CHARACTER != '-'
                ) {
                        return ABNORMAL;
                }
        }

        return NORMAL;
}

static const char * profile_bool_string(int8_t VALUE) {
        switch (VALUE) {
                case ISFALSE: return "false";
                case ISTRUE:  return "true";
                default:      return NULL;
        }
}

static const char * profile_rx_state_string(rx_state_t VALUE) {
        switch (VALUE) {
                case RXS_TRANSACTION: return "transaction";
                case RXS_STREAM:      return "stream";
                default:              return NULL;
        }
}

static const char * profile_tx_generation_string(tx_generation_t VALUE) {
        switch (VALUE) {
                case TXG_RANDOM:    return "random";
                case TXG_STATIC:    return "static";
                case TXG_INCREMENT: return "increment";
                default:            return NULL;
        }
}

static const char * profile_match_policy_string(match_policy_t VALUE) {
        switch (VALUE) {
                case MATCH_STRICT: return "strict";
                case MATCH_LOOSE:  return "loose";
                default:           return NULL;
        }
}

static const char * profile_checksum_mode_string(checksum_mode_t VALUE) {
        switch (VALUE) {
                case CHECKSUM_AUTO:    return "auto";
                case CHECKSUM_MANUAL: return "manual";
                default:               return NULL;
        }
}

static void profile_display_open_section(PROFILE_DISPLAY_SECTION *SECTION) {
        if (!SECTION || SECTION->OPENED == ISTRUE) {
                return;
        }

        if (*SECTION->PRINTED_ANY == ISTRUE) {
                kui_add_line("");
        }

        kui_add_line(ANSI_COLOR_CYAN "┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓");
        kui_add_line(
                "┃ "
                ANSI_COLOR_RESET
                "%-32s"
                ANSI_COLOR_CYAN
                " ┃",
                SECTION->TITLE
        );
        kui_add_line("┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛" ANSI_COLOR_RESET);

        SECTION->OPENED = ISTRUE;
        *SECTION->PRINTED_ANY = ISTRUE;
}

static void profile_display_row_string(
        PROFILE_DISPLAY_SECTION *SECTION,
        const char *KEY,
        const unsigned char *VALUE
) {
        const char * DISPLAY_VALUE;

        if (!VALUE) {
                return;
        }

        DISPLAY_VALUE = VALUE[0] == 0x00 ? "<empty>" : (const char *)VALUE;

        profile_display_open_section(SECTION);
        kui_add_line(
                "    "
                ANSI_COLOR_CYAN
                "%-32s"
                ANSI_COLOR_RESET
                " : "
                RGB_COLOR_BRIGHT_GREEN
                "%s"
                ANSI_COLOR_RESET,
                KEY,
                DISPLAY_VALUE
        );
}

static void profile_display_row_named(
        PROFILE_DISPLAY_SECTION *SECTION,
        const char *KEY,
        const char *VALUE
) {
        if (!VALUE) {
                return;
        }

        profile_display_open_section(SECTION);
        kui_add_line(
                "    "
                ANSI_COLOR_CYAN
                "%-32s"
                ANSI_COLOR_RESET
                " : "
                RGB_COLOR_BRIGHT_GREEN
                "%s"
                ANSI_COLOR_RESET,
                KEY,
                VALUE
        );
}

static void profile_display_row_int(
        PROFILE_DISPLAY_SECTION *SECTION,
        const char *KEY,
        int VALUE
) {
        if (VALUE == GLOBAL_RESET) {
                return;
        }

        profile_display_open_section(SECTION);
        kui_add_line(
                "    "
                ANSI_COLOR_CYAN
                "%-32s"
                ANSI_COLOR_RESET
                " : "
                RGB_COLOR_BRIGHT_GREEN
                "%d"
                ANSI_COLOR_RESET,
                KEY,
                VALUE
        );
}

static void profile_display_row_uint(
        PROFILE_DISPLAY_SECTION *SECTION,
        const char *KEY,
        unsigned int VALUE
) {
        if (VALUE == (unsigned int)GLOBAL_RESET) {
                return;
        }

        profile_display_open_section(SECTION);
        kui_add_line(
                "    "
                ANSI_COLOR_CYAN
                "%-32s"
                ANSI_COLOR_RESET
                " : "
                RGB_COLOR_BRIGHT_GREEN
                "%u"
                ANSI_COLOR_RESET,
                KEY,
                VALUE
        );
}

static void profile_display_row_uint64(
        PROFILE_DISPLAY_SECTION *SECTION,
        const char *KEY,
        uint64_t VALUE
) {
        if (VALUE == (uint64_t)GLOBAL_RESET) {
                return;
        }

        profile_display_open_section(SECTION);
        kui_add_line(
                "    "
                ANSI_COLOR_CYAN
                "%-32s"
                ANSI_COLOR_RESET
                " : "
                RGB_COLOR_BRIGHT_GREEN
                "%llu"
                ANSI_COLOR_RESET,
                KEY,
                (unsigned long long)VALUE
        );
}

////////////////////////////////////////////////////////////////////////////////
//      Primary Library Functions
////////////////////////////////////////////////////////////////////////////////

// @@ Display every active schema value in canonical INI order.
void profile_display_global(_carry_forward * _prog_data) {
        if (!_prog_data) {
                return;
        }

        GLOBAL_PROFILE *PROFILE = &_prog_data->gprof;
        int8_t PRINTED_ANY = ISFALSE;
        
        kui_add_line("< Loaded Profile:");
        kui_add_line("");

        PROFILE_DISPLAY_SECTION PROFILE_SECTION = {
                "PROFILE METADATA", ISFALSE, &PRINTED_ANY
        };
        PROFILE_DISPLAY_SECTION TX_SECTION = {
                "PACKET TRANSMISSION POLICY", ISFALSE, &PRINTED_ANY
        };
        PROFILE_DISPLAY_SECTION RX_SECTION = {
                "PACKET RECEIVE POLICY", ISFALSE, &PRINTED_ANY
        };
        PROFILE_DISPLAY_SECTION DNS_SECTION = {
                "DNS RESOLUTION POLICY", ISFALSE, &PRINTED_ANY
        };
        PROFILE_DISPLAY_SECTION LIMITS_SECTION = {
                "RESOURCE LIMITS", ISFALSE, &PRINTED_ANY
        };

        // [profile]
        profile_display_row_string(
                &PROFILE_SECTION, "profile.name", PROFILE->profile_name
        );
        profile_display_row_int(
                &PROFILE_SECTION, "profile.version", PROFILE->profile_schema_version
        );
        profile_display_row_string(
                &PROFILE_SECTION, "profile.scope", PROFILE->scope
        );

        // [tx]
        profile_display_row_string(
                &TX_SECTION, "tx.interface", PROFILE->tx_interface
        );
        profile_display_row_string(
                &TX_SECTION, "tx.source_ip", PROFILE->tx_source_ip
        );
        profile_display_row_string(
                &TX_SECTION, "tx.gateway", PROFILE->tx_gateway
        );
        profile_display_row_named(
                &TX_SECTION, "tx.strict",
                profile_bool_string(PROFILE->tx_strict)
        );
        profile_display_row_int(
                &TX_SECTION, "tx.ttl", PROFILE->tx_ipv4_ttl
        );
        profile_display_row_int(
                &TX_SECTION, "tx.hop_limit", PROFILE->tx_ipv6_hop_limit
        );
        profile_display_row_named(
                &TX_SECTION, "tx.ip_id_mode",
                profile_tx_generation_string(PROFILE->tx_ipv4_id_generation)
        );
        profile_display_row_named(
                &TX_SECTION, "tx.tcp_src_port_mode",
                profile_tx_generation_string(PROFILE->tx_tcp_src_port_generation)
        );
        profile_display_row_named(
                &TX_SECTION, "tx.checksum_mode",
                profile_checksum_mode_string(PROFILE->tx_checksum_mode)
        );

        // [rx]
        profile_display_row_string(
                &RX_SECTION, "rx.interface", PROFILE->rx_interface
        );
        profile_display_row_int(
                &RX_SECTION, "rx.timeout_ms", PROFILE->rx_timeout_ms
        );
        profile_display_row_named(
                &RX_SECTION, "rx.promiscuous",
                profile_bool_string(PROFILE->rx_promiscuous)
        );
        profile_display_row_int(
                &RX_SECTION, "rx.snaplength", PROFILE->rx_snaplength
        );
        profile_display_row_named(
                &RX_SECTION, "rx.state",
                profile_rx_state_string(PROFILE->rx_state)
        );
        profile_display_row_named(
                &RX_SECTION, "rx.dedupe",
                profile_bool_string(PROFILE->rx_dedupe)
        );
        profile_display_row_named(
                &RX_SECTION, "rx.match_policy",
                profile_match_policy_string(PROFILE->rx_match_policy)
        );

        // [dns]
        profile_display_row_string(
                &DNS_SECTION, "dns.server1", PROFILE->dns_server1
        );
        profile_display_row_string(
                &DNS_SECTION, "dns.server2", PROFILE->dns_server2
        );
        profile_display_row_string(
                &DNS_SECTION, "dns.search_domain", PROFILE->dns_search_domain
        );
        profile_display_row_int(
                &DNS_SECTION, "dns.timeout_ms", PROFILE->dns_timeout_ms
        );
        profile_display_row_int(
                &DNS_SECTION, "dns.retries", PROFILE->dns_retries
        );

        // [limits]
        profile_display_row_uint(
                &LIMITS_SECTION,
                "limits.max_pending_transactions",
                PROFILE->limit_max_pending_transactions
        );
        profile_display_row_uint(
                &LIMITS_SECTION,
                "limits.max_capture_bytes_per_tx",
                PROFILE->limit_max_capture_bytes_per_tx
        );
        profile_display_row_uint64(
                &LIMITS_SECTION,
                "limits.max_capture_bytes_total",
                PROFILE->limit_max_capture_bytes_total
        );
        profile_display_row_uint(
                &LIMITS_SECTION,
                "limits.max_tx_rate_pps",
                PROFILE->limit_max_tx_out_rate_pps
        );
        profile_display_row_uint(
                &LIMITS_SECTION,
                "limits.max_rx_rate_pps",
                PROFILE->limit_max_rx_in_rate_pps
        );
        profile_display_row_uint(
                &LIMITS_SECTION,
                "limits.max_dns_queries_pending",
                PROFILE->limit_max_dns_queries_pending
        );
        profile_display_row_uint(
                &LIMITS_SECTION,
                "limits.max_targets_per_batch",
                PROFILE->limit_max_targets_per_batch
        );
        profile_display_row_uint(
                &LIMITS_SECTION,
                "limits.max_scan_workers",
                PROFILE->limit_max_scan_workers
        );

        if (PRINTED_ANY == ISTRUE) {
                kui_add_line("");
        }
}

// @@ Display the complete profile command syntax.
void profile_print_usage(void) {
        kui_add_line(
                "< Usage: profile [ display | load | set | save ]"
        );
        kui_add_line(
                " \tprofile"
                ANSI_COLOR_CYAN
                "\t\t\t\t\t"
                ANSI_COLOR_RESET
                "Display the active runtime profile."
        );
        kui_add_line(
                "\tprofile load "
                ANSI_COLOR_CYAN
                "<Profile_Name>"
                ANSI_COLOR_RESET
                " \t\tLoad and validate a profile."
        );
        kui_add_line(
                "\tprofile set "
                ANSI_COLOR_CYAN
                "<section.key> <value>"
                ANSI_COLOR_RESET
                "\tChange active runtime data only."
        );
        kui_add_line(
                "\tprofile save "
                ANSI_COLOR_CYAN
                "<Profile_Name>"
                ANSI_COLOR_RESET
                "\t\tSave runtime data as a new profile."
        );
        kui_add_line(
                "\tprofile save "
                ANSI_COLOR_CYAN
                "<Profile_Name> overwrite"
                ANSI_COLOR_RESET
                "\tConfirm replacement of an existing profile."
        );
        kui_add_line(
                NOTICE_INFO
                "Bare keys are accepted only when unique; use section.key for interface and timeout_ms."
        );
        kui_add_line(
                NOTICE_INFO
                "Use the value 'empty' or \"\" to clear fields that permit an empty value."
        );
}

// @@ Display load syntax before enumerating available profiles.
void profile_print_load_usage(void) {
        kui_add_line(
                "< Usage: profile load ["
                ANSI_COLOR_CYAN
                "<Profile_Name>"
                ANSI_COLOR_RESET
                "]"
        );
}

// @@ Display regular .ini files in the user profile directory.
void profile_list_available(void) {
        DIR * PROFILES;
        struct dirent * ENTRY;
        struct stat INFO;
        char PROFILE_PATH[MAX_PATH];
        c_size_t NAME_LENGTH;
        int COUNT;

        PROFILES = opendir(KAMI_USER_PROFILES_DIR);

        if (!PROFILES) {
                kui_add_line(
                        NOTICE_ERROR
                        "Failure to open the profile directory."
                );
                return;
        }

        kui_add_line("< Available Profiles:");
        kui_add_line("" ANSI_COLOR_MAGENTA);

        COUNT = 0;

        while ((ENTRY = readdir(PROFILES)) != NULL) {
                if (
                        strcmp(ENTRY->d_name, ".") == MATCH
                        || strcmp(ENTRY->d_name, "..") == MATCH
                ) {
                        continue;
                }

                NAME_LENGTH = strlen(ENTRY->d_name);

                if (
                        NAME_LENGTH == 0
                        || profile_supported_filename(ENTRY->d_name) == ABNORMAL
                ) {
                        continue;
                }

                memset(PROFILE_PATH, 0x00, sizeof(PROFILE_PATH));

                if (
                        snprintf(
                                PROFILE_PATH,
                                sizeof(PROFILE_PATH),
                                "%s%s",
                                KAMI_USER_PROFILES_DIR,
                                ENTRY->d_name
                        ) >= (int)sizeof(PROFILE_PATH)
                ) {
                        continue;
                }

                memset(&INFO, 0x00, sizeof(INFO));

                if (lstat(PROFILE_PATH, &INFO) != NORMAL || !S_ISREG(INFO.st_mode)) {
                        continue;
                }

                COUNT++;
                kui_add_line(
                        "%d.)\t" ANSI_COLOR_CYAN "%s" ANSI_COLOR_MAGENTA,
                        COUNT,
                        ENTRY->d_name
                );
        }

        closedir(PROFILES);
        kui_add_line(ANSI_COLOR_RESET "");

        if (COUNT == 0) {
                kui_add_line("< No profiles found.");
        }
}

// @@ Load a named profile through the shared global parser/validator.
void profile_try_load(
        const unsigned char * PROFILE_NAME,
        _carry_forward * _prog_data
) {
        GLOBAL_PROFILE PREVIOUS_PROFILE;

        memcpy(&PREVIOUS_PROFILE, &_prog_data->gprof, sizeof(PREVIOUS_PROFILE));

        if (load_global_profile(PROFILE_NAME, _prog_data) == ABNORMAL) {
                kui_add_line(
                        NOTICE_WARNING "%s",
                        gprofile_last_error()
                );
                profile_print_load_usage();
                return;
        }

        if (_prog_data->nosix_net != NULL) {
                if (projects_nosix_reopen(_prog_data) != NOSIX_OK) {
                        memcpy(&_prog_data->gprof, &PREVIOUS_PROFILE, sizeof(PREVIOUS_PROFILE));
                        kui_add_line(
                                NOTICE_ERROR
                                "NOSIX network runtime reconfiguration failed (%d); profile change rolled back.",
                                _prog_data->nosix_status
                        );
                        return;
                }

                kui_add_line(
                        NOTICE_SUCCESS
                        "NOSIX network runtime reconfigured."
                );
        }

        kui_add_line(
                NOTICE_SUCCESS "Profile "
                ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET
                " successfully loaded.",
                _prog_data->gprof.profile_name
        );
}

// @@ Change active runtime data without touching a profile on disk.
void profile_try_set(
        const unsigned char * KEY,
        const unsigned char * VALUE,
        _carry_forward * _prog_data
) {
        const unsigned char * RUNTIME_VALUE;
        static const unsigned char EMPTY_VALUE[] = "";
        GLOBAL_PROFILE PREVIOUS_PROFILE;

        memcpy(&PREVIOUS_PROFILE, &_prog_data->gprof, sizeof(PREVIOUS_PROFILE));
        RUNTIME_VALUE = VALUE;

        if (
                VALUE
                && (
                        strcmp((const char *)VALUE, "empty") == MATCH
                        || strcmp((const char *)VALUE, "\"\"") == MATCH
                )
        ) {
                RUNTIME_VALUE = EMPTY_VALUE;
        }

        if (gprofile_set_runtime_value(KEY, RUNTIME_VALUE, _prog_data) == ABNORMAL) {
                kui_add_line(
                        NOTICE_WARNING "%s",
                        gprofile_last_error()
                );
                return;
        }

        if (_prog_data->nosix_net != NULL) {
                if (projects_nosix_reopen(_prog_data) != NOSIX_OK) {
                        memcpy(&_prog_data->gprof, &PREVIOUS_PROFILE, sizeof(PREVIOUS_PROFILE));
                        kui_add_line(
                                NOTICE_ERROR
                                "NOSIX network runtime reconfiguration failed (%d); runtime change rolled back.",
                                _prog_data->nosix_status
                        );
                        return;
                }

                kui_add_line(
                        NOTICE_SUCCESS
                        "NOSIX network runtime reconfigured."
                );
        }

        kui_add_line(
                NOTICE_SUCCESS "Runtime field "
                ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET
                " updated.",
                KEY
        );
        kui_add_line(
                NOTICE_INFO
                "The change is active in memory only; use 'profile save' to persist it."
        );
}

// @@ Save active runtime data under a validated profile name.
void profile_try_save(
        const unsigned char * PROFILE_NAME,
        int8_t ALLOW_OVERWRITE,
        _carry_forward * _prog_data
) {
        int RESULT;

        RESULT = gprofile_save_runtime(PROFILE_NAME, ALLOW_OVERWRITE, _prog_data);

        if (RESULT == GPROFILE_SAVE_EXISTS) {
                kui_add_line(
                        NOTICE_WARNING "%s",
                        gprofile_last_error()
                );
                kui_add_line(
                        "< Confirm overwrite with: profile save "
                        ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET
                        " overwrite",
                        PROFILE_NAME
                );
                return;
        }

        if (RESULT == ABNORMAL) {
                kui_add_line(
                        NOTICE_WARNING "%s",
                        gprofile_last_error()
                );
                return;
        }

        kui_add_line(
                NOTICE_SUCCESS "Profile "
                ANSI_COLOR_CYAN "%s.ini" ANSI_COLOR_RESET
                " safely saved.",
                _prog_data->gprof.profile_name
        );
}
