// Copyright 2026 Jamison A. Drapeau
// Standalone parser contract tests: cc -std=c11 -Wall -Wextra -Werror
// -Isrc/books tests/book_args_test.c src/books/book_args.c -o /tmp/book-args-test
#include "book_args.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static BOOK_ARGUMENTS args;
static char error[160];

static void good(const char * command, size_t count) {
        memset(&args, 0xa5, sizeof(args));
        memset(error, 0xa5, sizeof(error));
        assert(book_args_parse(command, "web-enum", &args, error, sizeof(error)) == 0);
        assert(error[0] == '\0');
        assert(args.count == count);
}

static void bad(const char * command) {
        assert(book_args_parse(command, "web-enum", &args, error, sizeof(error)) != 0);
        assert(error[0] != '\0');
}

int main(void) {
        char input[1100];

        // No-argument compatibility, leading/trailing whitespace.
        good("book web-enum", 0);
        good("  book web-enum  \n", 0);

        // Both public syntax forms, arbitrary author-defined names, and order.
        good("book web-enum --port 8443 --path=/health --scan_mode deep", 3);
        assert(strcmp(args.items[0].name, "port") == 0);
        assert(strcmp(args.items[0].value, "8443") == 0);
        assert(strcmp(args.items[1].name, "path") == 0);
        assert(strcmp(args.items[1].value, "/health") == 0);
        assert(strcmp(args.items[2].name, "scan_mode") == 0);
        assert(strcmp(args.items[2].value, "deep") == 0);

        // Spaces, quoting and equals within quoted values.
        good("book web-enum --path \"/a b\" --label='hello world'", 2);
        assert(strcmp(args.items[0].value, "/a b") == 0);
        assert(strcmp(args.items[1].value, "hello world") == 0);
        good("book web-enum --literal=\"--flag\" --empty ''", 2);
        assert(strcmp(args.items[0].value, "--flag") == 0);
        assert(strcmp(args.items[1].value, "") == 0);
        good("book web-enum --escaped \"a\\\"b\\\\c\"", 1);
        assert(strcmp(args.items[0].value, "a\"b\\c") == 0);

        // Reject malformed keys, missing values, duplicates, and positional args.
        bad("book web-enum --port");
        bad("book web-enum --port --path /");
        bad("book web-enum --port 443 --port 80");
        bad("book web-enum 443");
        bad("book web-enum --");
        bad("book web-enum --1port 443");
        bad("book web-enum --bad.name yes");
        bad("book web-enum --path \"/broken");
        bad("tool web-enum --port 443");
        bad("book ssh-kex --port 22");

        // Limit enforcement: no unbounded value, name, or argument count.
        memset(input, 'x', sizeof(input));
        memcpy(input, "book web-enum --long ", 21);
        input[sizeof(input) - 1] = '\0';
        bad(input);

        (void)snprintf(input, sizeof(input), "book web-enum");
        for (unsigned i = 0; i < BOOK_ARGS_LIMIT + 1; i++) {
                char fragment[24];
                (void)snprintf(fragment, sizeof(fragment), " --param%u v", i);
                (void)strcat(input, fragment);
        }
        bad(input);

        puts("Book argument parser contract: PASS");
        return 0;
}
