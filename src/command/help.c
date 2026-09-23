// Copyright 2026 Jamison A. Drapeau
#include "help.h"
#include "kui.h"
#include <string.h>
void help(_carry_forward * _prog_data) {
        if (_prog_data->cmd_tokens_count == 1) {

                // GLOBAL COMMAND LIST
                kui_add_line("< Usage: help [" ANSI_COLOR_CYAN "<command>" ANSI_COLOR_RESET "]");
                kui_add_line("");
                
                kui_add_line(ANSI_COLOR_CYAN "┏━━━━━━━━━━━━━━┓");
                kui_add_line("┃" ANSI_COLOR_RESET " COMMAND LIST " ANSI_COLOR_CYAN "┃");
                kui_add_line("┗━━━━━━━━━━━━━━┛" ANSI_COLOR_RESET);

                kui_add_line("");
                kui_add_line(
                        ANSI_COLOR_MAGENTA "[\t" ANSI_COLOR_RESET 
                        "help\t\t\t<-- Prints this help menu"
                        ANSI_COLOR_MAGENTA "\t\t] h" ANSI_COLOR_RESET
                );
                if (_prog_data->help_caller_state_manager == C_TOOL_CONTEXTUAL) {
                        kui_add_line(
                                ANSI_COLOR_MAGENTA "[\t" ANSI_COLOR_RESET
                                "back\t\t\t<-- Return to target context"
                                ANSI_COLOR_MAGENTA "\t\t]" ANSI_COLOR_RESET
                        );
                        kui_add_line(
                                ANSI_COLOR_MAGENTA "[\t" ANSI_COLOR_RESET
                                "list\t\t\t<-- List registered external tools"
                                ANSI_COLOR_MAGENTA "\t]" ANSI_COLOR_RESET
                        );
                        kui_add_line(
                                ANSI_COLOR_MAGENTA "[\t" ANSI_COLOR_RESET
                                "add\t\t\t<-- Register an external tool"
                                ANSI_COLOR_MAGENTA "\t\t]" ANSI_COLOR_RESET
                        );
                        kui_add_line(
                                ANSI_COLOR_MAGENTA "[\t" ANSI_COLOR_RESET
                                "del\t\t\t<-- Remove a registered external tool"
                                ANSI_COLOR_MAGENTA "\t]" ANSI_COLOR_RESET
                        );
                        kui_add_line(
                                ANSI_COLOR_MAGENTA "[\t" ANSI_COLOR_RESET
                                "<tool> [args]\t\t<-- Execute a registered external tool"
                                ANSI_COLOR_MAGENTA "\t]" ANSI_COLOR_RESET
                        );
                } else if (_prog_data->help_caller_state_manager == C_TARGETS_CONTEXTUAL) {
                        kui_add_line(
                                ANSI_COLOR_MAGENTA "[\t" ANSI_COLOR_RESET 
                                "back\t\t\t<-- Return to project"
                                ANSI_COLOR_MAGENTA "\t\t\t]" ANSI_COLOR_RESET
                        );
                        kui_add_line(
                                ANSI_COLOR_MAGENTA "[\t" ANSI_COLOR_RESET 
                                "set\t\t\t<-- Manually edit parameters"
                                ANSI_COLOR_MAGENTA "\t\t] s" ANSI_COLOR_RESET
                        );
                        kui_add_line(
                                ANSI_COLOR_MAGENTA "[\t" ANSI_COLOR_RESET
                                "display\t\t\t<-- Display target + book data"
                                ANSI_COLOR_MAGENTA "\t\t] dt" ANSI_COLOR_RESET
                        );
                }
                if (_prog_data->help_caller_state_manager == S_DEFAULT) {
                        kui_add_line(
                                ANSI_COLOR_MAGENTA "[\t" ANSI_COLOR_RESET 
                                "debug\t\t\t<-- For debug info/activation"
                                ANSI_COLOR_MAGENTA "\t\t]" ANSI_COLOR_RESET
                        );
                        kui_add_line( 
                                ANSI_COLOR_MAGENTA "[\t" ANSI_COLOR_RESET 
                                "pwd\t\t\t<-- Prints current directory"
                                ANSI_COLOR_MAGENTA "\t\t]" ANSI_COLOR_RESET
                        );
                        kui_add_line(
                              ANSI_COLOR_MAGENTA "[\t" ANSI_COLOR_RESET 
                                "interfaces\t\t<-- Display available interfaces"
                                ANSI_COLOR_MAGENTA "\t] i" ANSI_COLOR_RESET
                        );
                        kui_add_line(
                              ANSI_COLOR_MAGENTA "[\t" ANSI_COLOR_RESET 
                                "profile\t\t\t<-- Set or list global configurations"
                                ANSI_COLOR_MAGENTA "\t] p" ANSI_COLOR_RESET
                        );
                        kui_add_line( 
                                ANSI_COLOR_MAGENTA "[\t" ANSI_COLOR_RESET 
                                "new\t\t\t<-- Create a new project"
                                ANSI_COLOR_MAGENTA "\t\t] n" ANSI_COLOR_RESET
                        );
                        kui_add_line(
                                ANSI_COLOR_MAGENTA "[\t" ANSI_COLOR_RESET 
                                "load\t\t\t<-- Loads a project"
                                ANSI_COLOR_MAGENTA "\t\t\t] l" ANSI_COLOR_RESET
                        );
                        kui_add_line(
                                ANSI_COLOR_MAGENTA "[\t" ANSI_COLOR_RESET 
                                "unload\t\t\t<-- Unloads active project"
                                ANSI_COLOR_MAGENTA "\t\t] u" ANSI_COLOR_RESET
                        );
                        kui_add_line(
                                ANSI_COLOR_MAGENTA "[\t" ANSI_COLOR_RESET 
                                "targets\t\t\t<-- Modify or display Targets"
                                ANSI_COLOR_MAGENTA "\t\t] t/d/a (d = display)" ANSI_COLOR_RESET
                        );
                }
                if (_prog_data->help_caller_state_manager != C_TOOL_CONTEXTUAL) {
                        kui_add_line(
                                ANSI_COLOR_MAGENTA "[\t" ANSI_COLOR_RESET
                                "ping\t\t\t<-- Send an ICMP packet to a target"
                                ANSI_COLOR_MAGENTA "\t]" ANSI_COLOR_RESET
                        );
                        kui_add_line(
                                ANSI_COLOR_MAGENTA "[\t" ANSI_COLOR_RESET
                                "resolve\t\t\t<-- Use DNS to resolve URLs"
                                ANSI_COLOR_MAGENTA "\t\t] r" ANSI_COLOR_RESET
                        );
                        kui_add_line(
                                ANSI_COLOR_MAGENTA "[\t" ANSI_COLOR_RESET
                                "scan\t\t\t<-- Primary enumeration tool"
                                ANSI_COLOR_MAGENTA "\t\t]" ANSI_COLOR_RESET
                        );
                        kui_add_line(
                                ANSI_COLOR_MAGENTA "[\t" ANSI_COLOR_RESET
                                "book\t\t\t<-- User defined scan scripts"
                                ANSI_COLOR_MAGENTA "\t\t] b" ANSI_COLOR_RESET
                        );
                        kui_add_line(
                                ANSI_COLOR_MAGENTA "[\t" ANSI_COLOR_RESET
                                "tool\t\t\t<-- Manage or execute external tools"
                                ANSI_COLOR_MAGENTA "\t]" ANSI_COLOR_RESET
                        );
                }
                // KEEP LAST
                kui_add_line( 
                        ANSI_COLOR_MAGENTA "[\t" ANSI_COLOR_RESET 
                        "exit\t\t\t<-- Exit and return to shell"
                        ANSI_COLOR_MAGENTA "\t\t]" ANSI_COLOR_RESET
                );
                kui_add_line("");
        } else {
                // INDIVIDUAL USAGE STATEMENTS:
                if (strcmp((char*)_prog_data->cmd_tokens[1], "help") == MATCH) {
                        kui_add_line("< Usage: help [" ANSI_COLOR_CYAN "<command>" ANSI_COLOR_RESET "]");
                        kui_add_line(NOTICE_INFO "This is the help menu.");
                        kui_add_line(NOTICE_INFO "You can use 'help' followed by the command you need usage for.");
                        kui_add_line(NOTICE_INFO "When ran without arguments it will display the command list.");
                        kui_add_line(NOTICE_INFO "The magenta letters after the index for each command is an alias.");
                        kui_add_line(NOTICE_INFO "e.g. "ANSI_COLOR_MAGENTA"] h"ANSI_COLOR_RESET" <-- just enter 'h' and it will alias to 'help'");
                } else if (
                        _prog_data->help_caller_state_manager == C_TARGETS_CONTEXTUAL
                        &&
                        (
                                (strcmp((char*)_prog_data->cmd_tokens[1], "display") == MATCH)
                                ||
                                (strcmp((char*)_prog_data->cmd_tokens[1], "dt") == MATCH)
                                ||
                                (strcmp((char*)_prog_data->cmd_tokens[1], "d") == MATCH)
                                ||
                                (strcmp((char*)_prog_data->cmd_tokens[1], "dd") == MATCH)
                        )
                ) {
                        kui_add_line("< Usage: display");
                        kui_add_line(NOTICE_INFO "Displays the current target's built-in scan state first, followed by every stored book .out snapshot.");
                        kui_add_line(NOTICE_INFO "Target context has no per-book display selector.");
                        kui_add_line(
                                NOTICE_INFO
                                "Aliases: "
                                ANSI_COLOR_MAGENTA
                                "dt / d / dd"
                                ANSI_COLOR_RESET
                        );
                } else if (strcmp((char*)_prog_data->cmd_tokens[1], "debug") == MATCH) {
                        kui_add_line("< Usage: debug [ on | off ]");
                        kui_add_line(NOTICE_INFO "This will toggle debug mode on or off.");
                        kui_add_line(NOTICE_INFO "Mostly used during the developement of kaminowaku.");
                } else if (strcmp((char*)_prog_data->cmd_tokens[1], "pwd") == MATCH) {
                        kui_add_line("< Usage: pwd");
                        kui_add_line(NOTICE_INFO "Prints current working directory for kaminowaku.");
                        kui_add_line(NOTICE_INFO "Useful if you don't know where the kaminowaku datastore is on disk.");
                } else if (strcmp((char*)_prog_data->cmd_tokens[1], "interfaces") == MATCH) {
                        kui_add_line("< Usage: interfaces");
                        kui_add_line(NOTICE_INFO "Prints the available system interfaces.");
                        kui_add_line(NOTICE_INFO "Useful if you need to know which interfaces you can set up for the profile.");
                } else if (strcmp((char*)_prog_data->cmd_tokens[1], "profile") == MATCH) {
                        kui_add_line("< Usage: profile [ display | load | set | save ]");
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
                                "Bare keys are accepted only when unique; use section.key for interface, strict, and timeout_ms."
                        );
                        kui_add_line(
                                NOTICE_INFO
                                "Use the value 'empty' or \"\" to clear fields that permit an empty value."
                        );
                } else if (strcmp((char*)_prog_data->cmd_tokens[1], "new") == MATCH) {
                        kui_add_line("< Usage: new [" ANSI_COLOR_CYAN "<project_name>" ANSI_COLOR_RESET "]");
                        kui_add_line(NOTICE_INFO "Will create a new project with specified name");
                        kui_add_line(NOTICE_INFO "This command will enter contextual mode when passed without arguments.");
                        kui_add_line(NOTICE_INFO "Press enter without a project name buffered to cancel.");
                } else if (strcmp((char*)_prog_data->cmd_tokens[1], "load") == MATCH) {
                        kui_add_line("< Usage: load [" ANSI_COLOR_CYAN "<project_name>" ANSI_COLOR_RESET "]");
                        kui_add_line(NOTICE_INFO "Will load a project using specified name");
                        kui_add_line(NOTICE_INFO "This command will enter contextual mode when passed without arguments.");
                        kui_add_line(NOTICE_INFO "Press enter without a project name buffered to cancel.");
                } else if (strcmp((char*)_prog_data->cmd_tokens[1], "unload") == MATCH) {
                        kui_add_line("< Usage: unload");
                        kui_add_line(NOTICE_INFO "Will unload the currently loaded project.");
                        kui_add_line(NOTICE_INFO "Project data is saved automatically unless kaminowaku is forcefully terminated.");
                } else if (strcmp((char*)_prog_data->cmd_tokens[1], "targets") == MATCH) {
                        kui_add_line("< Usage:");
                        kui_add_line("");
                        // Targets add|del usage
                        kui_add_line(                
                                "\ttargets [add | del ["
                                ANSI_COLOR_CYAN         "<TID>"
                                ANSI_COLOR_RESET        "]] [-U "
                                ANSI_COLOR_CYAN         "<URL>"
                                ANSI_COLOR_RESET        "][-4 "
                                ANSI_COLOR_CYAN         "<IPv4 Address>"
                                ANSI_COLOR_RESET        "][-6 "
                                ANSI_COLOR_CYAN         "<IPv6 Address>"
                                ANSI_COLOR_RESET        "][-M "
                                ANSI_COLOR_CYAN         "<MAC Address>"
                                ANSI_COLOR_RESET        "]]"
                        );
                        kui_add_line(
                                "\t" NOTICE_INFO
                                ANSI_COLOR_YELLOW "Add or Delete a target." ANSI_COLOR_RESET
                        );
                        kui_add_line("");
                        kui_add_line("\ttargets del -n");
                        kui_add_line(
                                "\t" NOTICE_INFO
                                ANSI_COLOR_YELLOW "Delete targets with no received-packet observation." ANSI_COLOR_RESET
                        );
                        kui_add_line("");
                        // CIDR add usage:
                        kui_add_line(
                                "\ttargets add -net [-4 | -6] "
                                ANSI_COLOR_CYAN         "<CIDR Network>"
                                ANSI_COLOR_RESET
                        );
                        kui_add_line(
                                "\t" NOTICE_INFO
                                ANSI_COLOR_YELLOW "Expand an IPv4 or IPv6 CIDR network into individual targets." ANSI_COLOR_RESET
                        );
                        kui_add_line("");
                        // Target Context Usage:
                        kui_add_line(
                                "\ttargets [["
                                ANSI_COLOR_CYAN         "<TID>" 
                                ANSI_COLOR_RESET        "] | [-U46M] "
                                ANSI_COLOR_CYAN         "<Identifier>" 
                                ANSI_COLOR_RESET        "]"
                        );
                        kui_add_line(
                                "\t[set [-U46MN] "
                                ANSI_COLOR_CYAN         "<Value>" 
                                ANSI_COLOR_RESET        " | display | dt]"
                        );
                        kui_add_line(
                                "\t" NOTICE_INFO 
                                ANSI_COLOR_YELLOW "Enter into target context for display, modification, or scanning."  ANSI_COLOR_RESET
                        );
                        kui_add_line("");
                        // Targets display usage:
                        kui_add_line(
                                "\ttargets display [ -d | -o | -p " ANSI_COLOR_CYAN "<Ports>..." ANSI_COLOR_RESET " | -b " ANSI_COLOR_CYAN "<Ports>..." ANSI_COLOR_RESET " ]"
                        );
                        kui_add_line(
                                "\t" NOTICE_INFO 
                                ANSI_COLOR_YELLOW "Display all targets in a project with or without filters." ANSI_COLOR_RESET
                        );
                        kui_add_line("");
                        // Arg translations
                        kui_add_line("\t-net\t: Expand a CIDR network into individual targets (add only)");
                        kui_add_line("\t-U\t: URL");
                        kui_add_line("\t-4\t: IPv4 Address");
                        kui_add_line("\t-6\t: IPv6 Address");
                        kui_add_line("\t-M\t: MAC Address");
                        kui_add_line("\t-N\t: Note (e.g. 'DC' or 'Webserver' or 'Bobs Computer')");
                        kui_add_line("\t-n\t: Delete targets with no received-packet observation (del only)");
                        kui_add_line("\t-d\t: Display full persisted scan details followed by every target's stored Book output");
                        kui_add_line("\t-o\t: Display only targets and scan results backed by received packets");
                        kui_add_line("\t-p\t: Display targets with any selected TCP port OPEN; show only selected TCP port observations; Book output remains collapsed");
                        kui_add_line("\t-b\t: Display targets with any selected TCP port OPEN; add detailed built-in banner/service data for selected OPEN TCP ports; Book output remains collapsed");
                        kui_add_line("\t\t  Port expressions accept single values, ranges, comma-separated combinations, and multiple expressions.");
                        kui_add_line("");
                } else if (strcmp((char*)_prog_data->cmd_tokens[1], "ping") == MATCH) {
                        kui_add_line("< Usage: ping [ -4 | -6 ]");
                        kui_add_line(NOTICE_INFO "While in the project context, will send an ICMP packet to every target.");
                        kui_add_line(NOTICE_INFO "While in a target context, will send an ICMP packet to that target.");
                } else if (strcmp((char*)_prog_data->cmd_tokens[1], "resolve") == MATCH) {
                        kui_add_line("< Usage: resolve");
                        kui_add_line(NOTICE_INFO "Extract IPv4 and IPv6 addresses using DNS resolution.");
                } else if (strcmp((char*)_prog_data->cmd_tokens[1], "scan") == MATCH) {
                        kui_add_line("< Usage: scan [ -F | -t " ANSI_COLOR_CYAN "<ports>" ANSI_COLOR_RESET " | -u " ANSI_COLOR_CYAN "<ports>" ANSI_COLOR_RESET " ]");
                        kui_add_line(NOTICE_INFO "TCP and UDP accept single ports, ranges, and comma-separated combinations.");
                        kui_add_line(NOTICE_INFO "Examples: scan -t 22,80,443 | scan -t 1-1024 -u 53,123 | scan -F");
                        kui_add_line(NOTICE_INFO "-F scans TCP and UDP ports 1-65535.");
                        kui_add_line(NOTICE_INFO "Project context scans every target; target context scans only the active target.");
                        kui_add_line(NOTICE_INFO "Open TCP results are automatically enriched with passive banners and safe HTTP/TLS probes.");
                } else if (strcmp((char*)_prog_data->cmd_tokens[1], "tool") == MATCH) {
                        kui_add_line(
                                "< Usage: tool [ add " ANSI_COLOR_CYAN "<absolute-path>" ANSI_COLOR_RESET
                                " | list | del " ANSI_COLOR_CYAN "<tool-name>" ANSI_COLOR_RESET " ]"
                        );
                        kui_add_line(NOTICE_INFO "Run tool without arguments from a target context to enter [tools]> mode.");
                        kui_add_line(NOTICE_INFO "tool add registers an executable by absolute path in the Kaminowaku toolbox.");
                        kui_add_line(NOTICE_INFO "tool list displays registered external tools.");
                        kui_add_line(NOTICE_INFO "Inside [tools]> context, use add <absolute-path>, list, and del <tool-name> directly.");
                        kui_add_line(NOTICE_INFO "tool del removes a toolbox registration without deleting the underlying executable.");
                        kui_add_line(NOTICE_INFO "Registered tools execute directly without a shell and use the active target directory as their working directory.");
                        kui_add_line(NOTICE_INFO "Interactive tool sessions run through a PTY; live output is rendered through KUI under the " NOTICE_CHURNING " indicator.");
                        kui_add_line(NOTICE_INFO "Raw PTY output is saved in the active target directory as <tool>-<epoch_ns>.out.");
                        kui_add_line(NOTICE_INFO "External tool execution does not create a Kaminowaku PCAP session or update Kami TX/RX counters.");
                } else if (strcmp((char*)_prog_data->cmd_tokens[1], "book") == MATCH) {
                        kui_add_line("< Usage: book [ " ANSI_COLOR_CYAN "<book-name>" ANSI_COLOR_RESET " ]");
                        kui_add_line(NOTICE_INFO "Run " ANSI_COLOR_CYAN "book" ANSI_COLOR_RESET " with no arguments to list all available system/user books.");
                        kui_add_line(NOTICE_INFO "Run a canonical system/user scan book through a capture-gated BOOK_SESSION.");
                        kui_add_line(NOTICE_INFO "Project context runs the book sequentially against every target; target context runs only the active target.");
                        kui_add_line(NOTICE_INFO "Successful output atomically replaces BOOKNAME.out; failures and bail preserve the previous snapshot.");
                        kui_add_line(NOTICE_INFO "Every executed target retains its timestamped PCAP artifact.");
                        kui_add_line(NOTICE_INFO "Book names may contain letters, numbers, hyphens, and underscores.");
                        kui_add_line(NOTICE_INFO "Alias: " ANSI_COLOR_MAGENTA "b" ANSI_COLOR_RESET);
                } else if (strcmp((char*)_prog_data->cmd_tokens[1], "") == MATCH) { // TEMPLATE

                } else {
                        kui_add_line("< Command not recognized or no additional help available.");
                }   
        }
        _prog_data->f_type = _prog_data->help_caller_state_manager;
        return;
}
