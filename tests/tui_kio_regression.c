// Copyright 2026 Jamison A. Drapeau
// @@ Exercise KIO itself, including the unread tail between read_line calls.
#include "kio.h"
#include "kui.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static int scroll_sum;
void kui_input_begin(void) {}
void kui_input_end(void) {}
void kui_input_update(const char *prompt, const char *input, unsigned int cursor) {
        (void)prompt; (void)input; (void)cursor;
}
void kui_input_scroll(int rows) { scroll_sum += rows; }
void kui_input_bell(void) {}
void kui_input_refresh_page(void) {}
void kui_add_line(const char *fmt, ...) { (void)fmt; }

static int start_pipe(void) {
        int fds[2];
        assert(pipe(fds) == 0);
        assert(dup2(fds[0], STDIN_FILENO) == STDIN_FILENO);
        close(fds[0]);
        return fds[1];
}

int main(void) {
        _carry_forward *data = calloc(1, sizeof(*data));
        assert(data);
        int saved_stdin = dup(STDIN_FILENO);
        assert(saved_stdin >= 0);

        // @@ Buffered characters after Enter survive to the next command.
        int fd = start_pipe();
        assert(write(fd, "alpha\nbeta\n", 11) == 11);
        close(fd);
        assert(read_line(data) == 6);
        assert(strcmp((const char *)data->cmd_input, "alpha\n") == 0);
        assert(read_line(data) == 5);
        assert(strcmp((const char *)data->cmd_input, "beta\n") == 0);

        // @@ SGR mouse event split across real system reads.
        fd = start_pipe();
        pid_t writer = fork();
        assert(writer >= 0);
        if (writer == 0) {
                assert(write(fd, "\033[<64;61;", 9) == 9);
                usleep(20000);
                assert(write(fd, "71Mxyz\n", 7) == 7);
                close(fd);
                _exit(0);
        }
        close(fd);
        scroll_sum = 0;
        assert(read_line(data) == 4);
        assert(scroll_sum == 3);
        assert(strcmp((const char *)data->cmd_input, "xyz\n") == 0);
        int status = 0;
        assert(waitpid(writer, &status, 0) == writer);
        assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);

        // @@ Bracketed multiline paste must not execute its inner newline.
        fd = start_pipe();
        const char *paste = "\033[200~scan\nhelp\033[201~\n";
        assert(write(fd, paste, strlen(paste)) == (ssize_t)strlen(paste));
        close(fd);
        assert(read_line(data) == (int)strlen("scan help\n"));
        assert(strcmp((const char *)data->cmd_input, "scan help\n") == 0);

        assert(dup2(saved_stdin, STDIN_FILENO) == STDIN_FILENO);
        close(saved_stdin);
        free(data);
        puts("PASS: command tails, fragmented SGR wheel and multiline bracketed paste");
        return 0;
}
