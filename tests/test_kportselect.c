// Copyright 2026 Jamison A. Drapeau
#include "kportselect.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static unsigned int assertions = 0;

#define CHECK(C) do { assertions++; if (!(C)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #C); return 1; \
} } while (0)

static int write_fixture(const char * path, const char * body) {
        FILE * f = fopen(path, "w");
        if (!f) return -1;
        fputs("# KAMINOWAKU PORTS v1\n", f);
        fputs("# timestamp_ns\tprotocol\taf\tport\tstate\tevidence\trx_bytes\n", f);
        fputs(body, f);
        return fclose(f);
}

int main(void) {
        char path[] = "/tmp/kportselect.XXXXXX";
        int fd = mkstemp(path);
        KPORT_SPEC ports = {0};
        if (fd < 0) return 2;
        close(fd);

        CHECK(kportspec_parse("22,80,443,8080", &ports) == 0);

        CHECK(write_fixture(path,
                "100\tTCP\t4\t22\tOPEN\tSYN+ACK\t60\n"
                "100\tTCP\t4\t80\tCLOSED\tRST\t60\n") == 0);
        CHECK(kportselect_file_has_open_tcp_ports(path, &ports) == 1);

        CHECK(write_fixture(path,
                "100\tTCP\t4\t22\tOPEN\tSYN+ACK\t60\n"
                "200\tTCP\t4\t22\tCLOSED\tRST\t60\n") == 0);
        CHECK(kportselect_file_has_open_tcp_ports(path, &ports) == 0);

        CHECK(write_fixture(path,
                "300\tTCP\t4\t443\tCLOSED\tRST\t60\n"
                "250\tTCP\t6\t443\tOPEN\tSYN+ACK\t80\n") == 0);
        CHECK(kportselect_file_has_open_tcp_ports(path, &ports) == 1);

        CHECK(write_fixture(path,
                "100\tUDP\t4\t22\tOPEN\tUDP_REPLY\t40\n"
                "100\tTCP\t4\t25\tOPEN\tSYN+ACK\t60\n") == 0);
        CHECK(kportselect_file_has_open_tcp_ports(path, &ports) == 0);

        CHECK(write_fixture(path,
                "100\tTCP\t4\t8080\tOPEN\tSYN+ACK\t60\n"
                "100\tTCP\t4\t8080\tCLOSED\tRST\t60\n") == 0);
        CHECK(kportselect_file_has_open_tcp_ports(path, &ports) == 0);

        CHECK(write_fixture(path,
                "200\tTCP\t4\t80\tCLOSED\tRST\t60\n"
                "100\tTCP\t4\t80\tOPEN\tSYN+ACK\t60\n"
                "150\tTCP\t6\t80\tFILTERED\tTIMEOUT\t0\n") == 0);
        CHECK(kportselect_file_has_open_tcp_ports(path, &ports) == 0);

        CHECK(write_fixture(path,
                "bad line\n"
                "100\tTCP\t9\t22\tOPEN\tSYN+ACK\t60\n"
                "100\tTCP\t4\t22\tBOGUS\tSYN+ACK\t60\n"
                "100\tTCP\t6\t22\tOPEN\tSYN+ACK\t60\n") == 0);
        CHECK(kportselect_file_has_open_tcp_ports(path, &ports) == 1);

        KPORT_SPEC none = {0};
        CHECK(kportselect_file_has_open_tcp_ports(path, &none) == 0);
        CHECK(kportselect_file_has_open_tcp_ports(NULL, &ports) == 0);
        CHECK(kportselect_file_has_open_tcp_ports(path, NULL) == 0);

        unlink(path);
        printf("PASS: kportselect (%u assertions)\n", assertions);
        return 0;
}
