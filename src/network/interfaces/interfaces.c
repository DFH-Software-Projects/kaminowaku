// Copyright 2026 Jamison A. Drapeau
#include "interfaces.h"
#include "kui.h"
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <stdio.h>
#include <string.h>
#include <netinet/in.h>
#include <sys/socket.h>

// @@ Display all network interfaces and assigned IP addresses
void interfaces(void) {
        struct if_nameindex * INTERFACES = if_nameindex();
        struct if_nameindex * CURRENT = NULL;
        struct ifaddrs * ADDRESS_LIST = NULL;
        struct ifaddrs * ADDRESS = NULL;
        const void * ADDRESS_SOURCE = NULL;
        char IP_ADDRESS[INET6_ADDRSTRLEN];
        memset(IP_ADDRESS, 0x00, INET6_ADDRSTRLEN);
        int ADDRESS_FAMILY = 0;
        int ADDRESS_FOUND = 0;

        // Check to see if the struct for the interfaces initialized properly
        if (INTERFACES == NULL) {
                kui_add_line(
                        NOTICE_ERROR "Failed to enumerate network interfaces."
                );
                return;
        }

        // Validate adress pulls
        if (getifaddrs(&ADDRESS_LIST) != 0) {
                kui_add_line(
                        NOTICE_ERROR "Failed to enumerate interface addresses."
                );
                if_freenameindex(INTERFACES);
                return;
        }

        // Notify
        kui_add_line("< System Interfaces:");
        kui_add_line("");

        // Primary table loop
        CURRENT = INTERFACES;
        while (CURRENT->if_index != 0 && CURRENT->if_name != NULL) {
                ADDRESS_FOUND = ISFALSE;
                for (
                        ADDRESS = ADDRESS_LIST;
                        ADDRESS != NULL;
                        ADDRESS = ADDRESS->ifa_next
                ) {
                        if (
                                ADDRESS->ifa_name == NULL
                                ||
                                ADDRESS->ifa_addr == NULL
                                ||
                                strcmp(ADDRESS->ifa_name, CURRENT->if_name) != MATCH
                        ) {
                                continue;
                        }
                        ADDRESS_FAMILY = ADDRESS->ifa_addr->sa_family;
                        ADDRESS_SOURCE = NULL;
                        if (ADDRESS_FAMILY == AF_INET) {
                                const struct sockaddr_in * IPV4_ADDRESS;
                                IPV4_ADDRESS = (const struct sockaddr_in *)ADDRESS->ifa_addr;
                                ADDRESS_SOURCE = &IPV4_ADDRESS->sin_addr;
                        } else if (ADDRESS_FAMILY == AF_INET6) {
                                const struct sockaddr_in6 * IPV6_ADDRESS;
                                IPV6_ADDRESS = (const struct sockaddr_in6 *)ADDRESS->ifa_addr;
                                ADDRESS_SOURCE = &IPV6_ADDRESS->sin6_addr;
                        } else {
                                continue;
                        }
                        memset(IP_ADDRESS, 0x00, sizeof(IP_ADDRESS));
                        if (
                                inet_ntop(
                                        ADDRESS_FAMILY,
                                        ADDRESS_SOURCE,
                                        IP_ADDRESS,
                                        sizeof(IP_ADDRESS)
                                ) == NULL
                        ) {
                                continue;
                        }

                        // ------------------------------------------
                        kui_add_line(
                                ANSI_COLOR_CYAN
                                "[%u]"
                                ANSI_COLOR_RESET
                                "\t%-16s\t%s",
                                CURRENT->if_index,
                                CURRENT->if_name,
                                IP_ADDRESS
                        );
                        // ------------------------------------------
                        ADDRESS_FOUND = ISTRUE;
                }

                // If no address assigned
                if (ADDRESS_FOUND == ISFALSE) {

                        // ------------------------------------------
                        kui_add_line(
                                ANSI_COLOR_CYAN
                                "[%u]"
                                ANSI_COLOR_RESET
                                "\t%-16s\t"
                                RGB_COLOR_BRIGHT_YELLOW
                                "No IP assigned"
                                ANSI_COLOR_RESET,
                                CURRENT->if_index,
                                CURRENT->if_name
                        );
                        // ------------------------------------------
                }
                CURRENT++;
        }
        kui_add_line("");
        freeifaddrs(ADDRESS_LIST);
        if_freenameindex(INTERFACES);
}