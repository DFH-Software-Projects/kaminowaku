// Copyright 2026 Jamison A. Drapeau
// Integration smoke: the actual Book parser, runtime, and native globals,
// without a network interface, PCAP capture, or external services.
#include "book_args.h"
#include "book_parser.h"
#include "book_runtime.h"
#include "book_session.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
        session->started_ns = 1;
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

int main(void) {
        // Two independent sessions: populated args and backward-compatible empty args.
        check_run("book arg-smoke --port 8443 --path \"/health check\" "
                  "--custom-name 'alpha beta' --mode=deep", 4);
        check_run("book arg-smoke", 0);
        puts("Native Lua Book args integration: PASS");
        return 0;
}
