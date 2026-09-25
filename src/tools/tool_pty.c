// Copyright 2026 Jamison A. Drapeau
#if defined(__FreeBSD__)
#define _BSD_SOURCE
#else
#define _XOPEN_SOURCE 700
#endif

#include "tool_pty.h"
#include "kui.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

typedef enum {
        TOOL_PTY_ESC_NONE = 0,
        TOOL_PTY_ESC_SEEN,
        TOOL_PTY_ESC_CSI,
        TOOL_PTY_ESC_OSC,
        TOOL_PTY_ESC_OSC_ESC
} tool_pty_escape_state_t;

typedef struct {
        char line[KUI_MAX_SCROLL_COLS];
        c_size_t line_length;
        int8_t overwrite_pending;
        tool_pty_escape_state_t escape_state;
} TOOL_PTY_STREAM;

static volatile sig_atomic_t TOOL_PTY_RESIZED = 0;

#define TOOL_PTY_ARTIFACT_COLLISION_LIMIT 100
#define TOOL_PTY_RENDER_INTERVAL_NS 33333333ULL // @@ At most 30 visual updates/second.

static uint64_t tool_pty_now_ns(void) {
        struct timespec timestamp;

        if (clock_gettime(CLOCK_REALTIME, &timestamp) != NORMAL) return 0;

        return (
                (uint64_t)timestamp.tv_sec * 1000000000ULL
                + (uint64_t)timestamp.tv_nsec
        );
}

// @@ The raw PTY artifact is always captured at full rate. Only screen
// painting is throttled to avoid holding up high-volume external tools.
static uint64_t tool_pty_monotonic_ns(void) {
        struct timespec now;
        if (clock_gettime(CLOCK_MONOTONIC, &now) != NORMAL) return 0;
        return (uint64_t)now.tv_sec * 1000000000ULL + (uint64_t)now.tv_nsec;
}

static int tool_pty_artifact_open(
        const char * tool_name,
        const char * working_directory,
        char * artifact_path,
        c_size_t artifact_path_size
) {
        uint64_t timestamp_ns;
        unsigned int collision = 0;

        if (
                !tool_name
                || !working_directory
                || !artifact_path
                || artifact_path_size == 0
        ) {
                return ABNORMAL;
        }

        timestamp_ns = tool_pty_now_ns();
        if (timestamp_ns == 0) return ABNORMAL;

        while (collision < TOOL_PTY_ARTIFACT_COLLISION_LIMIT) {
                int written;
                int artifact_fd;

                memset(artifact_path, 0x00, artifact_path_size);

                written = collision == 0
                        ? snprintf(
                                artifact_path,
                                artifact_path_size,
                                "%s%s%s-%" PRIu64 ".out",
                                working_directory,
                                working_directory[strlen(working_directory) - 1] == '/' ? "" : "/",
                                tool_name,
                                timestamp_ns
                        )
                        : snprintf(
                                artifact_path,
                                artifact_path_size,
                                "%s%s%s-%" PRIu64 "-%u.out",
                                working_directory,
                                working_directory[strlen(working_directory) - 1] == '/' ? "" : "/",
                                tool_name,
                                timestamp_ns,
                                collision
                        );

                if (
                        written <= 0
                        || (c_size_t)written >= artifact_path_size
                ) {
                        return ABNORMAL;
                }

                artifact_fd = open(
                        artifact_path,
                        O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC,
                        0600
                );
                if (artifact_fd >= 0) return artifact_fd;

                if (errno != EEXIST) return ABNORMAL;
                collision++;
        }

        return ABNORMAL;
}

static void tool_pty_sigwinch(int signo) {
        (void)signo;
        TOOL_PTY_RESIZED = 1;
}

static int tool_pty_write_all(
        int fd,
        const unsigned char * buffer,
        c_size_t length
) {
        c_size_t offset = 0;

        while (offset < length) {
                ssize_t written = write(
                        fd,
                        buffer + offset,
                        length - offset
                );

                if (written > 0) {
                        offset += (c_size_t)written;
                        continue;
                }

                if (written < 0 && errno == EINTR) continue;
                return ABNORMAL;
        }

        return NORMAL;
}

static int tool_pty_apply_winsize(int master_fd) {
        struct winsize window;

        if (!isatty(STDOUT_FILENO)) return NORMAL;
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &window) != NORMAL) return ABNORMAL;
        if (ioctl(master_fd, TIOCSWINSZ, &window) != NORMAL) return ABNORMAL;

        return NORMAL;
}

static int tool_pty_terminal_raw(
        struct termios * saved,
        int8_t * saved_valid
) {
        struct termios raw;

        if (!saved || !saved_valid) return ABNORMAL;

        *saved_valid = ISFALSE;
        if (!isatty(STDIN_FILENO)) return NORMAL;

        if (tcgetattr(STDIN_FILENO, saved) != NORMAL) return ABNORMAL;

        raw = *saved;
        raw.c_lflag &= ~(ECHO | ICANON | ISIG);
        raw.c_cc[VMIN] = 1;
        raw.c_cc[VTIME] = 0;

        if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != NORMAL) return ABNORMAL;

        *saved_valid = ISTRUE;
        return NORMAL;
}

static void tool_pty_terminal_restore(
        const struct termios * saved,
        int8_t saved_valid
) {
        if (
                saved_valid == ISTRUE
                && saved
                && isatty(STDIN_FILENO)
        ) {
                (void)tcsetattr(STDIN_FILENO, TCSAFLUSH, saved);
        }
}

static void tool_pty_stream_status(
        const char * tool_name,
        const TOOL_PTY_STREAM * stream
) {
        if (!tool_name || !stream) return;

        if (stream->line_length > 0) {
                kui_set_churning(
                        NOTICE_CHURNING
                        ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET
                        " | %s",
                        tool_name,
                        stream->line
                );
        } else {
                kui_set_churning(
                        NOTICE_CHURNING
                        ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET,
                        tool_name
                );
        }
}

static void tool_pty_stream_commit(TOOL_PTY_STREAM * stream) {
        if (!stream) return;

        stream->line[stream->line_length] = 0x00;
        kui_add_line("%s", stream->line);

        memset(stream->line, 0x00, sizeof(stream->line));
        stream->line_length = 0;
        stream->overwrite_pending = ISFALSE;
}

static void tool_pty_stream_push(
        TOOL_PTY_STREAM * stream,
        unsigned char value
) {
        if (!stream) return;

        if (stream->overwrite_pending == ISTRUE) {
                memset(stream->line, 0x00, sizeof(stream->line));
                stream->line_length = 0;
                stream->overwrite_pending = ISFALSE;
        }

        if (stream->line_length >= KUI_MAX_SCROLL_COLS - 1) {
                tool_pty_stream_commit(stream);
        }

        stream->line[stream->line_length++] = (char)value;
        stream->line[stream->line_length] = 0x00;
}

static void tool_pty_stream_byte(
        TOOL_PTY_STREAM * stream,
        unsigned char value
) {
        if (!stream) return;

        switch (stream->escape_state) {
                case TOOL_PTY_ESC_SEEN:
                        if (value == '[') {
                                stream->escape_state = TOOL_PTY_ESC_CSI;
                        } else if (value == ']') {
                                stream->escape_state = TOOL_PTY_ESC_OSC;
                        } else {
                                stream->escape_state = TOOL_PTY_ESC_NONE;
                        }
                        return;

                case TOOL_PTY_ESC_CSI:
                        if (value >= 0x40 && value <= 0x7E) {
                                stream->escape_state = TOOL_PTY_ESC_NONE;
                        }
                        return;

                case TOOL_PTY_ESC_OSC:
                        if (value == 0x07) {
                                stream->escape_state = TOOL_PTY_ESC_NONE;
                        } else if (value == 0x1B) {
                                stream->escape_state = TOOL_PTY_ESC_OSC_ESC;
                        }
                        return;

                case TOOL_PTY_ESC_OSC_ESC:
                        if (value == '\\') {
                                stream->escape_state = TOOL_PTY_ESC_NONE;
                        } else if (value != 0x1B) {
                                stream->escape_state = TOOL_PTY_ESC_OSC;
                        }
                        return;

                case TOOL_PTY_ESC_NONE:
                default:
                        break;
        }

        if (value == 0x1B) {
                stream->escape_state = TOOL_PTY_ESC_SEEN;
                return;
        }

        if (value == '\r') {
                stream->overwrite_pending = ISTRUE;
                return;
        }

        if (value == '\n') {
                if (
                        stream->overwrite_pending == ISTRUE
                        && stream->line_length == 0
                ) {
                        stream->overwrite_pending = ISFALSE;
                        return;
                }

                tool_pty_stream_commit(stream);
                return;
        }

        if (value == '\b') {
                if (stream->overwrite_pending == ISTRUE) {
                        memset(stream->line, 0x00, sizeof(stream->line));
                        stream->line_length = 0;
                        stream->overwrite_pending = ISFALSE;
                }

                if (stream->line_length > 0) {
                        stream->line_length--;
                        stream->line[stream->line_length] = 0x00;
                }
                return;
        }

        if (value == '\t') {
                tool_pty_stream_push(stream, value);
                return;
        }

        if (value >= 0x20 && value != 0x7F) {
                tool_pty_stream_push(stream, value);
        }
}

static void tool_pty_stream_buffer(
        const char * tool_name,
        TOOL_PTY_STREAM * stream,
        const unsigned char * buffer,
        c_size_t length,
        uint64_t * last_render_ns
) {
        uint64_t now;
        if (!tool_name || !stream || !buffer || !last_render_ns) return;

        for (c_size_t i = 0; i < length; i++) {
                tool_pty_stream_byte(stream, buffer[i]);
        }

        // @@ Only presentation is rate-limited. Stream commits and the raw
        // tool artifact preserve every complete line and received byte.
        now = tool_pty_monotonic_ns();
        if (now != 0 && *last_render_ns != 0 && now >= *last_render_ns
                && now - *last_render_ns < TOOL_PTY_RENDER_INTERVAL_NS)
                return;

        tool_pty_stream_status(tool_name, stream);
        kui_render_page();
        *last_render_ns = now;
}

static int tool_pty_open_master(
        int * master_fd,
        char * slave_name,
        c_size_t slave_name_size
) {
        int fd;
        char * name;

        if (!master_fd || !slave_name || slave_name_size == 0) return ABNORMAL;

        fd = posix_openpt(O_RDWR | O_NOCTTY);
        if (fd < 0) return ABNORMAL;

        if (grantpt(fd) != NORMAL || unlockpt(fd) != NORMAL) {
                close(fd);
                return ABNORMAL;
        }

        name = ptsname(fd);
        if (!name) {
                close(fd);
                return ABNORMAL;
        }

        if (
                snprintf(
                        slave_name,
                        slave_name_size,
                        "%s",
                        name
                ) < 0
                || strlen(name) >= slave_name_size
        ) {
                close(fd);
                return ABNORMAL;
        }

        *master_fd = fd;
        return NORMAL;
}

static void tool_pty_child_close_fds(void) {
#if defined(__FreeBSD__)
        closefrom(STDERR_FILENO + 1);
#else
        long max_fd = sysconf(_SC_OPEN_MAX);

        if (max_fd < 0) max_fd = 1024;

        for (int fd = STDERR_FILENO + 1; fd < max_fd; fd++) {
                (void)close(fd);
        }
#endif
}

static void tool_pty_child(
        int master_fd,
        const char * slave_name,
        const char * executable_path,
        char * const child_argv[],
        const char * working_directory
) {
        int slave_fd;

        close(master_fd);

        if (setsid() < 0) _exit(126);

        slave_fd = open(slave_name, O_RDWR);
        if (slave_fd < 0) _exit(126);

        if (ioctl(slave_fd, TIOCSCTTY, 0) != NORMAL) {
                dprintf(
                        slave_fd,
                        "kaminowaku: unable to establish tool PTY: %s\n",
                        strerror(errno)
                );
                close(slave_fd);
                _exit(126);
        }

        (void)tcsetpgrp(slave_fd, getpgrp());

        if (
                dup2(slave_fd, STDIN_FILENO) < 0
                || dup2(slave_fd, STDOUT_FILENO) < 0
                || dup2(slave_fd, STDERR_FILENO) < 0
        ) {
                dprintf(
                        slave_fd,
                        "kaminowaku: unable to attach tool PTY: %s\n",
                        strerror(errno)
                );
                close(slave_fd);
                _exit(126);
        }

        if (slave_fd > STDERR_FILENO) close(slave_fd);

        tool_pty_child_close_fds();

        if (chdir(working_directory) != NORMAL) {
                dprintf(
                        STDERR_FILENO,
                        "kaminowaku: unable to enter target directory: %s\n",
                        strerror(errno)
                );
                _exit(126);
        }

        execv(executable_path, child_argv);

        dprintf(
                STDERR_FILENO,
                "kaminowaku: unable to execute %s: %s\n",
                child_argv && child_argv[0] ? child_argv[0] : "tool",
                strerror(errno)
        );
        _exit(126);
}

int tool_pty_run(
        const char * tool_name,
        const char * executable_path,
        char * const child_argv[],
        const char * working_directory,
        int * wait_status
) {
        int master_fd = -1;
        int artifact_fd = -1;
        char slave_name[MAX_PATH];
        char artifact_path[MAX_PATH];
        pid_t child;
        int status = 0;
        int8_t child_done = ISFALSE;
        int8_t master_done = ISFALSE;
        int8_t wait_status_valid = ISTRUE;
        int8_t artifact_write_valid = ISTRUE;
        unsigned int child_drain_idle = 0;
        uint64_t last_render_ns = 0;
        struct termios saved_terminal;
        int8_t saved_terminal_valid = ISFALSE;
        struct sigaction resize_action;
        struct sigaction old_resize_action;
        int8_t resize_action_valid = ISFALSE;
        TOOL_PTY_STREAM stream;

        if (
                !tool_name
                || !executable_path
                || !child_argv
                || !working_directory
                || !wait_status
        ) {
                return ABNORMAL;
        }

        memset(slave_name, 0x00, sizeof(slave_name));
        memset(artifact_path, 0x00, sizeof(artifact_path));
        memset(&saved_terminal, 0x00, sizeof(saved_terminal));
        memset(&resize_action, 0x00, sizeof(resize_action));
        memset(&old_resize_action, 0x00, sizeof(old_resize_action));
        memset(&stream, 0x00, sizeof(stream));

        artifact_fd = tool_pty_artifact_open(
                tool_name,
                working_directory,
                artifact_path,
                sizeof(artifact_path)
        );
        if (artifact_fd < 0) {
                kui_add_line(
                        NOTICE_ERROR
                        "Unable to create external tool output artifact."
                );
                return ABNORMAL;
        }

        kui_add_line(
                NOTICE_INFO
                "Output: " ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET,
                artifact_path
        );

        if (
                tool_pty_open_master(
                        &master_fd,
                        slave_name,
                        sizeof(slave_name)
                ) != NORMAL
        ) {
                close(artifact_fd);
                artifact_fd = -1;
                (void)unlink(artifact_path);
                kui_add_line(
                        NOTICE_ERROR
                        "Unable to allocate external tool PTY."
                );
                return ABNORMAL;
        }

        (void)tool_pty_apply_winsize(master_fd);

        if (
                tool_pty_terminal_raw(
                        &saved_terminal,
                        &saved_terminal_valid
                ) != NORMAL
        ) {
                close(master_fd);
                close(artifact_fd);
                artifact_fd = -1;
                (void)unlink(artifact_path);
                kui_add_line(
                        NOTICE_ERROR
                        "Unable to prepare terminal for external tool session."
                );
                return ABNORMAL;
        }

        resize_action.sa_handler = tool_pty_sigwinch;
        sigemptyset(&resize_action.sa_mask);
        if (
                sigaction(
                        SIGWINCH,
                        &resize_action,
                        &old_resize_action
                ) == NORMAL
        ) {
                resize_action_valid = ISTRUE;
        }

        TOOL_PTY_RESIZED = 0;
        kui_processing_begin();
        tool_pty_stream_status(tool_name, &stream);
        kui_render_page();

        // @@ Stop and join the renderer before fork: no pthread-owned
        // terminal, stdio or queue locks may be inherited by the child.
        kui_fork_prepare();
        fflush(NULL);
        child = fork();
        if (child != 0) kui_fork_parent();
        if (child < 0) {
                if (resize_action_valid == ISTRUE) {
                        (void)sigaction(SIGWINCH, &old_resize_action, NULL);
                }
                tool_pty_terminal_restore(
                        &saved_terminal,
                        saved_terminal_valid
                );
                kui_clear_churning();
                kui_render_page();
                kui_processing_end();
                close(master_fd);
                close(artifact_fd);
                artifact_fd = -1;
                (void)unlink(artifact_path);

                kui_add_line(
                        NOTICE_ERROR
                        "Unable to create external tool PTY process."
                );
                return ABNORMAL;
        }

        if (child == 0) {
                tool_pty_child(
                        master_fd,
                        slave_name,
                        executable_path,
                        child_argv,
                        working_directory
                );
        }

        while (
                child_done != ISTRUE
                || master_done != ISTRUE
        ) {
                struct pollfd pollfds[2];
                nfds_t poll_count = 1;
                int poll_result;

                memset(pollfds, 0x00, sizeof(pollfds));

                pollfds[0].fd = master_fd;
                pollfds[0].events = POLLIN | POLLHUP | POLLERR;

                if (
                        child_done != ISTRUE
                        && isatty(STDIN_FILENO)
                ) {
                        pollfds[1].fd = STDIN_FILENO;
                        pollfds[1].events = POLLIN;
                        poll_count = 2;
                }

                if (TOOL_PTY_RESIZED) {
                        TOOL_PTY_RESIZED = 0;
                        (void)tool_pty_apply_winsize(master_fd);
                        // @@ Reflow the Kaminowaku viewport even when the child
                        // emits no bytes after SIGWINCH.
                        kui_render_page();
                }

                poll_result = poll(pollfds, poll_count, 100);
                if (poll_result < 0 && errno != EINTR) {
                        if (child_done != ISTRUE) {
                                if (kill(-child, SIGHUP) != NORMAL) {
                                        (void)kill(child, SIGHUP);
                                }
                        }
                        wait_status_valid = ISFALSE;
                        break;
                }

                if (
                        poll_result > 0
                        && poll_count == 2
                        && (pollfds[1].revents & POLLIN)
                ) {
                        unsigned char input_buffer[256];
                        ssize_t input_length = read(
                                STDIN_FILENO,
                                input_buffer,
                                sizeof(input_buffer)
                        );

                        if (input_length > 0) {
                                if (
                                        tool_pty_write_all(
                                                master_fd,
                                                input_buffer,
                                                (c_size_t)input_length
                                        ) != NORMAL
                                ) {
                                        master_done = ISTRUE;
                                }
                        }
                }

                if (
                        poll_result > 0
                        && (
                                pollfds[0].revents & (
                                        POLLIN
                                        | POLLHUP
                                        | POLLERR
                                )
                        )
                ) {
                        unsigned char output_buffer[1024];
                        ssize_t output_length = read(
                                master_fd,
                                output_buffer,
                                sizeof(output_buffer)
                        );

                        if (output_length > 0) {
                                if (
                                        artifact_write_valid == ISTRUE
                                        && tool_pty_write_all(
                                                artifact_fd,
                                                output_buffer,
                                                (c_size_t)output_length
                                        ) != NORMAL
                                ) {
                                        artifact_write_valid = ISFALSE;
                                }

                                tool_pty_stream_buffer(
                                        tool_name,
                                        &stream,
                                        output_buffer,
                                        (c_size_t)output_length,
                                        &last_render_ns
                                );
                        } else if (
                                output_length == 0
                                || (
                                        output_length < 0
                                        && errno == EIO
                                )
                        ) {
                                master_done = ISTRUE;
                        }
                }

                if (child_done != ISTRUE) {
                        pid_t waited = waitpid(
                                child,
                                &status,
                                WNOHANG | WUNTRACED
                        );

                        if (waited == child) {
                                if (WIFSTOPPED(status)) {
                                        if (kill(-child, SIGCONT) != NORMAL) {
                                                (void)kill(child, SIGCONT);
                                        }
                                        kui_add_line(
                                                NOTICE_INFO
                                                "Tool suspension is not supported; continuing "
                                                ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET ".",
                                                tool_name
                                        );
                                        kui_render_page();
                                        kui_processing_begin();
                                } else {
                                        child_done = ISTRUE;
                                }
                        } else if (
                                waited < 0
                                && errno != EINTR
                        ) {
                                child_done = ISTRUE;
                                wait_status_valid = ISFALSE;
                        }
                }

                if (
                        child_done == ISTRUE
                        && master_done != ISTRUE
                ) {
                        if (poll_result == 0) {
                                child_drain_idle++;
                                if (child_drain_idle >= 10) {
                                        master_done = ISTRUE;
                                }
                        } else {
                                child_drain_idle = 0;
                        }
                }
        }

        if (child_done != ISTRUE) {
                pid_t waited;

                if (wait_status_valid != ISTRUE) {
                        if (kill(-child, SIGKILL) != NORMAL) {
                                (void)kill(child, SIGKILL);
                        }
                }

                do {
                        waited = waitpid(child, &status, 0);
                } while (waited < 0 && errno == EINTR);

                if (waited != child) {
                        wait_status_valid = ISFALSE;
                }
        }

        if (stream.line_length > 0) {
                tool_pty_stream_commit(&stream);
        }

        if (resize_action_valid == ISTRUE) {
                (void)sigaction(SIGWINCH, &old_resize_action, NULL);
        }

        tool_pty_terminal_restore(
                &saved_terminal,
                saved_terminal_valid
        );

        // @@ The raw output and final partial line have already been saved.
        // A single final paint clears the status indicator.
        kui_clear_churning();
        kui_render_page();
        kui_processing_end();

        close(master_fd);

        if (artifact_fd >= 0) {
                if (fsync(artifact_fd) != NORMAL) {
                        artifact_write_valid = ISFALSE;
                }
                close(artifact_fd);
                artifact_fd = -1;
        }

        if (artifact_write_valid != ISTRUE) {
                kui_add_line_and_render(
                        NOTICE_WARNING
                        "Tool output artifact may be incomplete: "
                        ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET ".",
                        artifact_path
                );
        }

        if (wait_status_valid != ISTRUE) {
                kui_add_line(
                        NOTICE_ERROR
                        "Unable to collect external tool PTY process."
                );
                return ABNORMAL;
        }

        *wait_status = status;
        return NORMAL;
}
