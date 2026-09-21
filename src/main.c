// Copyright 2026 Jamison A. Drapeau
#include "kaminowaku.h"
#include "data.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
int main(void) {

        // Root permission enforcement occurs during S_STARTUP in kaminowaku.c.

        // Generate data structure
        static _carry_forward _prog_data;
        
        // Set initial prompt
        memset(_prog_data.prompt, 0x00, DOUBLE_BLOCK);
        strcpy((char *)_prog_data.prompt, "> ");

        // Set debug switch to ISTRUE or ISFALSE depending on compile choice
        _prog_data.debug_flag = ISFALSE;

        // Set and scrub initial program data, refer to data.h for standards
        _prog_data.f_type = S_STARTUP;

        // TUI Init
        _prog_data.term_cols = RESET;
        _prog_data.term_rows = RESET;
        memset(&_prog_data.kui_scrollback, 0x00, sizeof(_prog_data.kui_scrollback));
        _prog_data.render_mode = RESET;
        
        // Clean IO memory
        _prog_data.cmd_tokens_count = TOKENS_INITIAL_SIZE; // 0
        _prog_data.cmd_tokens = NULL;
        memset(_prog_data.cmd_output, 0x00, OUT_BLOCK);
        memset(_prog_data.cmd_input, 0x00, INPUT_BLOCK);
        memset(_prog_data.cmd_last, 0x00, INPUT_BLOCK);
        memset(_prog_data.wd, 0x00, MAX_BLOCK);
        _prog_data.cmd_history_count = RESET;
        memset(_prog_data.cmd_history, 0x00, sizeof(_prog_data.cmd_history));

        //##############################################################################################
        // @@ Sanitize/Initialize any stateful variables you create here defined in data.h
        //##############################################################################################

        //===============
        // GLOBAL PROFILE
        //===============
        
        // Scrub the complete profile structure before assigning invalid sentinels.
        memset(&_prog_data.gprof, 0x00, sizeof(_prog_data.gprof));

        // Profile Metadata -----------------------------------------------------------
        memset(_prog_data.gprof.profile_name, 0x00, sizeof(_prog_data.gprof.profile_name));
        memset(_prog_data.gprof.scope, 0x00, sizeof(_prog_data.gprof.scope));
        _prog_data.gprof.profile_schema_version = GLOBAL_RESET;

        // Packet Transmission Policy ----------------------------------------
        memset(_prog_data.gprof.tx_interface, 0x00, sizeof(_prog_data.gprof.tx_interface));
        memset(_prog_data.gprof.tx_source_ip, 0x00, sizeof(_prog_data.gprof.tx_source_ip));
        memset(_prog_data.gprof.tx_gateway, 0x00, sizeof(_prog_data.gprof.tx_gateway));
        _prog_data.gprof.tx_strict                    = GLOBAL_RESET;
        _prog_data.gprof.tx_checksum_mode             = CHECKSUM_UNSET;
        _prog_data.gprof.tx_ipv4_id_generation        = TXG_UNSET;
        _prog_data.gprof.tx_tcp_src_port_generation   = TXG_UNSET;
        _prog_data.gprof.tx_ipv6_hop_limit            = GLOBAL_RESET;
        _prog_data.gprof.tx_ipv4_ttl                  = GLOBAL_RESET;
        
        // Packet Retrieval/Recieve Policy -----------------------------------
        memset(_prog_data.gprof.rx_interface, 0x00, sizeof(_prog_data.gprof.rx_interface));
        _prog_data.gprof.rx_promiscuous          = GLOBAL_RESET;
        _prog_data.gprof.rx_timeout_ms           = GLOBAL_RESET;
        _prog_data.gprof.rx_snaplength           = GLOBAL_RESET;
        _prog_data.gprof.rx_match_policy         = MATCH_UNSET;
        _prog_data.gprof.rx_state                = RXS_UNSET;
        _prog_data.gprof.rx_dedupe               = GLOBAL_RESET;

        // DNS Resolution Policy ---------------------------------------------
        memset(_prog_data.gprof.dns_server1, 0x00, sizeof(_prog_data.gprof.dns_server1));
        memset(_prog_data.gprof.dns_server2, 0x00, sizeof(_prog_data.gprof.dns_server2));
        memset(_prog_data.gprof.dns_search_domain, 0x00, sizeof(_prog_data.gprof.dns_search_domain));
        _prog_data.gprof.dns_timeout_ms  = GLOBAL_RESET;
        _prog_data.gprof.dns_retries     = GLOBAL_RESET;
        
        // Resource Limits ------------------------------------------------------------
        _prog_data.gprof.limit_max_pending_transactions  = (unsigned int)GLOBAL_RESET;
        _prog_data.gprof.limit_max_capture_bytes_per_tx  = (unsigned int)GLOBAL_RESET;
        _prog_data.gprof.limit_max_dns_queries_pending   = (unsigned int)GLOBAL_RESET;
        _prog_data.gprof.limit_max_targets_per_batch     = (unsigned int)GLOBAL_RESET;
        _prog_data.gprof.limit_max_scan_workers          = (unsigned int)GLOBAL_RESET;
        _prog_data.gprof.limit_max_tx_out_rate_pps       = (unsigned int)GLOBAL_RESET;
        _prog_data.gprof.limit_max_rx_in_rate_pps        = (unsigned int)GLOBAL_RESET;
        _prog_data.gprof.limit_max_capture_bytes_total   = (uint64_t)GLOBAL_RESET;

        //===============
        // NOSIX RUNTIME
        //===============
        _prog_data.nosix_net                                      = NULL;
        _prog_data.nosix_status                                   = NOSIX_OK;

        //====================================================================
        // Set a couple of initial project flags and identifiers to init state
        //====================================================================
        memset(_prog_data.active_project, 0x00, MAX_BLOCK);
        _prog_data.active_project_has_been_scanned              = RESET;
        _prog_data.active_project_last_scan_ns                  = RESET;
        _prog_data.active_project_has_targets                   = RESET;
        _prog_data.active_project_target_count                  = RESET;
        _prog_data.active_project_flower                        = NULL;
        _prog_data.unload_from_new                              = RESET;
        _prog_data.new_from_unload                              = RESET;
        _prog_data.unload_from_load                             = RESET;
        _prog_data.load_from_unload                             = RESET;
        _prog_data.help_caller_state_manager                    = S_DEFAULT;
        _prog_data.active_project_active_target                 = NULL;
        _prog_data.active_project_active_target_context         = RESET;
        _prog_data.active_project_active_target_directory       = NULL;
        _prog_data.total_tx_bytes                               = RESET;
        _prog_data.total_rx_bytes                               = RESET;

        //##############################################################################################

        // Transition to prompt
        // @@ CORE LOOP
        //////////////////////////////////
        while ( kaminowaku(&_prog_data) );
        //////////////////////////////////

        // @@ Free any dynamic arrays you define here:
        //----------------------------------------------------------------------------------------------------
        if (_prog_data.active_project_flower != NULL) {

                if (_prog_data.active_project_flower->PETAL != NULL) {
                        free(_prog_data.active_project_flower->PETAL);
                        _prog_data.active_project_flower->PETAL = NULL;
                }

                free(_prog_data.active_project_flower);
                _prog_data.active_project_flower = NULL;
        }
         //----------------------------------------------------------------------------------------------------

        // Exit
        return 0;
}