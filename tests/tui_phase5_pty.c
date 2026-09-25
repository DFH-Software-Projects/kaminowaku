// Copyright 2026 Jamison A. Drapeau
// @@ PTY kernel contract: fork/exec, size propagation, raw bytes and restore.
#define _XOPEN_SOURCE 700
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

static int contains(const char *buffer, size_t len, const char *needle, size_t n) {
        for (size_t i = 0; i + n <= len; i++)
                if (memcmp(buffer + i, needle, n) == 0) return 1;
        return 0;
}

int main(void) {
        int master = posix_openpt(O_RDWR | O_NOCTTY);
        assert(master >= 0 && grantpt(master) == 0 && unlockpt(master) == 0);
        char *slave_name = ptsname(master);
        assert(slave_name);
        int slave = open(slave_name, O_RDWR | O_NOCTTY);
        assert(slave >= 0);

        struct winsize ws = {0};
        ws.ws_row = 35;
        ws.ws_col = 110;
        assert(ioctl(master, TIOCSWINSZ, &ws) == 0);
        memset(&ws, 0, sizeof(ws));
        assert(ioctl(slave, TIOCGWINSZ, &ws) == 0);
        assert(ws.ws_row == 35 && ws.ws_col == 110);

        struct termios saved, raw, restored;
        assert(tcgetattr(slave, &saved) == 0);
        raw = saved;
        raw.c_lflag &= (tcflag_t)~(ECHO | ICANON);
        assert(tcsetattr(slave, TCSANOW, &raw) == 0);
        assert(tcsetattr(slave, TCSANOW, &saved) == 0);
        assert(tcgetattr(slave, &restored) == 0);
        assert(restored.c_lflag == saved.c_lflag);
        close(slave);

        pid_t child = fork();
        assert(child >= 0);
        if (child == 0) {
                close(master);
                assert(setsid() >= 0);
                int fd = open(slave_name, O_RDWR);
                assert(fd >= 0 && ioctl(fd, TIOCSCTTY, 0) == 0);
                assert(dup2(fd, STDIN_FILENO) >= 0);
                assert(dup2(fd, STDOUT_FILENO) >= 0);
                assert(dup2(fd, STDERR_FILENO) >= 0);
                if (fd > STDERR_FILENO) close(fd);
                execl("/bin/sh", "sh", "-c",
                        "printf '\\033[31mPTY-CAPTURE\\033[0m\\n'", (char *)NULL);
                _exit(126);
        }

        char output[512];
        size_t total = 0;
        FILE *artifact = tmpfile();
        assert(artifact);
        while (total < sizeof(output)) {
                ssize_t n = read(master, output + total, sizeof(output) - total);
                if (n < 0 && errno == EINTR) continue;
                if (n < 0 && errno == EIO) break; // @@ Linux PTY EOF
                if (n <= 0) break;
                assert(fwrite(output + total, 1, (size_t)n, artifact) == (size_t)n);
                total += (size_t)n;
        }
        int status = 0;
        assert(waitpid(child, &status, 0) == child);
        assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
        assert(contains(output, total, "\033[31m", 5));
        assert(contains(output, total, "PTY-CAPTURE", 11));
        rewind(artifact);
        char replay[512];
        assert(fread(replay, 1, total, artifact) == total);
        assert(memcmp(output, replay, total) == 0);
        fclose(artifact);
        close(master);
        puts("PASS: PTY fork/exec, terminal restore, resize, ANSI and byte-perfect artifact");
        return 0;
}
