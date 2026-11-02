// Copyright 2026 Jamison A. Drapeau
// Integration smoke: the actual Book parser, runtime, and native globals,
// without a network interface, PCAP capture, or external services.
#include "book_args.h"
#include "book_help.h"
#include "book_parser.h"
#include "book_runtime.h"
#include "book_session.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void check_run(const char * command, size_t count) {
        BOOK_ARGUMENTS * params = calloc(1, sizeof(*params));
        BOOK_SESSION * session = calloc(1, sizeof(*session));
        BOOK_PROGRAM * program = calloc(1, sizeof(*program));
        BOOK_MULTI_VALUE * returns = calloc(1, sizeof(*returns));
        FLOWER * flower = calloc(1, sizeof(*flower));
        TARGET * target = calloc(1, sizeof(*target));
        BOOK_RUNTIME * runtime = NULL;
        char error[160];

        assert(params && session && program && returns && flower && target);
        flower->PETAL = target;

        // The same parser invoked by books_try_run hands off its owned table.
        assert(book_args_parse(command, "arg-smoke", params, error, sizeof(error)) == 0);
        assert(params->count == count);
        session->arguments = params;
        session->target = flower;
        // Mirror the real Book session's monotonic execution deadline;
        // this network-free test bypasses books_session_prepare().
        struct timespec now;
        assert(clock_gettime(CLOCK_MONOTONIC, &now) == 0);
        session->started_ns = 1;
        session->execution_deadline_ns =
                (uint64_t)now.tv_sec * 1000000000ULL
                + (uint64_t)now.tv_nsec
                + 30000000000ULL;
        session->state = BOOK_SESSION_RUNTIME;
        (void)snprintf((char *)session->book_name, sizeof(session->book_name), "arg-smoke");
        (void)snprintf((char *)flower->TID, sizeof(flower->TID), "SMOKE123");

        assert(book_parser_parse_file((const unsigned char *)"tests/fixtures/arg-smoke.lua", program) == NORMAL);
        runtime = book_runtime_create(program, session);
        if (!runtime) {
                fprintf(stderr, "Failed to initialize Book Lua runtime\n");
                exit(1);
        }

        if (book_runtime_execute(runtime, returns) != NORMAL) {
                fprintf(stderr, "Lua args smoke failed @ %u:%u: %s\n",
                        book_runtime_error_line(runtime),
                        book_runtime_error_column(runtime),
                        book_runtime_error(runtime));
                exit(1);
        }

        book_runtime_destroy(runtime);
        book_program_free(program);
        free(target);
        free(flower);
        free(returns);
        free(program);
        free(session);
        free(params);
}

static void check_help(void) {
        BOOK_HELP help;

        // The built-in Book declaration is inspectable without an active
        // target, transport, runtime, or network capture.
        assert(book_help_read((const unsigned char*)"books/ssh-kex.lua",
                "ssh-kex", &help) == 0);
        assert(strstr(help.description, "SSH") != NULL);
        assert(help.option_count == 1);
        assert(strcmp(help.options[0].name, "port") == 0);
        assert(strstr(help.options[0].description, "22") != NULL);

        assert(book_help_read((const unsigned char*)"books/web-enum.lua",
                "web-enum", &help) == 0);
        assert(help.option_count == 3);
        assert(strcmp(help.options[0].name, "port") == 0);
        assert(strcmp(help.options[1].name, "path") == 0);
        assert(strcmp(help.options[2].name, "tls") == 0);

        assert(book_help_read((const unsigned char*)"books/cve-2026-110264.lua",
                "cve-2026-110264", &help) == 0);
        assert(help.option_count == 4);
        assert(strcmp(help.options[3].name, "timeout") == 0);

        // Existing community Books with only name metadata get generic help.
        assert(book_help_read((const unsigned char*)"tests/fixtures/arg-smoke.lua",
                "arg-smoke", &help) == 0);
        assert(help.option_count == 0);
        assert(help.description[0] == '\0');

        // Never evaluate top-level Book script when reading help metadata.
        assert(book_help_read((const unsigned char*)"tests/fixtures/help-noexec.lua",
                "help-noexec", &help) == 0);
        assert(help.option_count == 1);
        assert(strcmp(help.options[0].name, "custom-name") == 0);
        assert(book_help_read((const unsigned char*)"tests/fixtures/help-noexec.lua",
                "wrong-book", &help) != 0);
}

int main(void) {
        // Two independent sessions: populated args and backward-compatible empty args.
        check_run("book arg-smoke --port 8443 --path \"/health check\" "
                  "--custom-name 'alpha beta' --mode=deep", 4);
        check_run("book arg-smoke", 0);
        check_help();
        puts("Native Lua Book args integration: PASS");
        return 0;
}
