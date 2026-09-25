// Copyright 2026 Jamison A. Drapeau
// This code was heavily generated with the help of ChatGPT
// Code was refactored by me slightly for styling and behaviors.
#include "kio.h"
#include "kui.h"
#include "kio_escape.h"
#include <termios.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>

#define BLEN 1024 // @@ Burst Buffer Length = INPUT_BLOCK

static struct termios orig_termios;
static int kio_restore_registered = ISFALSE;

#ifndef TAB_STOP
#define TAB_STOP 8
#endif

/* ================== Terminal mode ================== */

void disable_raw_mode(void) {
        tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
}

void enable_raw_mode(void) {
        tcgetattr(STDIN_FILENO, &orig_termios);
        if (!kio_restore_registered) {
                (void)atexit(disable_raw_mode);
                kio_restore_registered = ISTRUE;
        }

        struct termios raw = orig_termios;
        raw.c_lflag &= ~(ECHO | ICANON);
        raw.c_cc[VMIN] = 1;   // wait for at least 1 byte
        raw.c_cc[VTIME] = 0;  // no read timeout
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);
}

// @@ Unread bytes after Enter belong to the next prompt, never a blind drain.
static unsigned char KIO_PENDING[BLEN];
static size_t KIO_PENDING_COUNT;
static KIO_DECODER KIO_DECODER_STATE;

static ssize_t kio_read_burst(unsigned char *buffer, size_t cap) {
        struct pollfd pfd = {.fd = STDIN_FILENO, .events = POLLIN};
        if (KIO_PENDING_COUNT) {
                size_t n = KIO_PENDING_COUNT < cap ? KIO_PENDING_COUNT : cap;
                memcpy(buffer, KIO_PENDING, n);
                KIO_PENDING_COUNT -= n;
                memmove(KIO_PENDING, KIO_PENDING + n, KIO_PENDING_COUNT);
                return (ssize_t)n;
        }
        for (;;) {
                int ready = poll(&pfd, 1, 75);
                if (ready == 0 || (ready < 0 && errno == EINTR)) return -2;
                if (ready < 0 || (pfd.revents & (POLLERR | POLLNVAL))) return -1;
                if (!(pfd.revents & (POLLIN | POLLHUP))) continue;
                ssize_t n = read(STDIN_FILENO, buffer, cap);
                if (n < 0 && errno == EINTR) return -2;
                return n;
        }
}

static void kio_keep_input(const unsigned char *bytes, size_t length) {
        if (length > sizeof(KIO_PENDING) - KIO_PENDING_COUNT)
                length = sizeof(KIO_PENDING) - KIO_PENDING_COUNT;
        if (length) {
                memcpy(KIO_PENDING + KIO_PENDING_COUNT, bytes, length);
                KIO_PENDING_COUNT += length;
        }
}

/* ================== Command history ================== */

static int command_history_has_content(const unsigned char *command, size_t length) {
        size_t i;
        if (!command) return ISFALSE;
        for (i = 0; i < length; i++) {
                if (command[i] != ' ' && command[i] != '\t')
                        return ISTRUE;
        }
        return ISFALSE;
}

static void command_history_store(
        _carry_forward *_prog_data,
        const unsigned char *command,
        size_t length
) {
        unsigned int destination;
        unsigned int oldest;

        if (!_prog_data || !command) return;
        if (length >= INPUT_BLOCK) length = INPUT_BLOCK - 1;
        if (command_history_has_content(command, length) == ISFALSE) return;

        oldest = (_prog_data->cmd_history_count < CMD_HISTORY_LIMIT)
                ? _prog_data->cmd_history_count + 1
                : CMD_HISTORY_LIMIT;

        for (destination = oldest; destination > 1; destination--) {
                memcpy(
                        _prog_data->cmd_history[destination],
                        _prog_data->cmd_history[destination - 1],
                        INPUT_BLOCK
                );
        }

        memset(_prog_data->cmd_history[1], 0x00, INPUT_BLOCK);
        memcpy(_prog_data->cmd_history[1], command, length);
        _prog_data->cmd_history[1][length] = 0x00;

        if (_prog_data->cmd_history_count < CMD_HISTORY_LIMIT)
                _prog_data->cmd_history_count++;
}

static void command_history_load(
        _carry_forward *_prog_data,
        unsigned int history_index,
        int *length_marker,
        int *cursor
) {
        size_t length;

        if (!_prog_data || !length_marker || !cursor) return;
        if (history_index > CMD_HISTORY_LIMIT) return;

        memset(_prog_data->cmd_input, 0x00, INPUT_BLOCK);
        memcpy(
                _prog_data->cmd_input,
                _prog_data->cmd_history[history_index],
                INPUT_BLOCK
        );
        _prog_data->cmd_input[INPUT_BLOCK - 1] = 0x00;

        length = strlen((char *)_prog_data->cmd_input);
        *length_marker = (int)length;
        *cursor = (int)length;
}

static void command_history_notice(
        _carry_forward *_prog_data,
        const char *message,
        int cursor
) {
        if (!_prog_data || !message) return;

        kui_add_line("%s", message);
        kui_input_refresh_page();

        /* The renderer re-anchors the prompt after painting the new page. */
        kui_input_update((char *)_prog_data->prompt, (char *)_prog_data->cmd_input, (unsigned int)cursor);
}


/* ================== Main line input ================== */

// @@ Batch wheel events without reordering intervening keystrokes.
static void kio_flush_wheel(int *wheel) {
        if (!wheel || !*wheel) return;
        kui_input_scroll(*wheel);
        *wheel = 0;
}

int read_line(_carry_forward *data) {
        int len = 0, cursor = 0, wheel = 0;
        unsigned history = 0, idle = 0, rows = 0, cols = 0;
        int8_t boundary_notified = ISFALSE;
        struct winsize ws = {0};
        if (!data) return -1;
        memset(data->cmd_input, 0, INPUT_BLOCK);
        memset(data->cmd_history[0], 0, INPUT_BLOCK);
        // @@ A cancelled/truncated bracketed paste must never poison the next prompt.
        kio_decoder_reset(&KIO_DECODER_STATE);
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0)
                { rows = ws.ws_row; cols = ws.ws_col; }
        kui_input_begin();
        kui_input_update((char *)data->prompt, (char *)data->cmd_input, 0);

        for (;;) {
                unsigned char burst[BLEN];
                ssize_t n = kio_read_burst(burst, sizeof(burst));
                if (n == -2) {
                        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 &&
                                (rows != ws.ws_row || cols != ws.ws_col)) {
                                rows = ws.ws_row; cols = ws.ws_col;
                                kio_flush_wheel(&wheel);
                                kui_input_refresh_page();
                        }
                        if (++idle >= 4) {
                                kio_decoder_timeout(&KIO_DECODER_STATE);
                                idle = 0;
                        }
                        continue;
                }
                if (n <= 0) {
                        kio_flush_wheel(&wheel);
                        kui_input_end();
                        return -1;
                }
                idle = 0;
                for (ssize_t i = 0; i < n; i++) {
                        KIO_TOKEN token = kio_decoder_feed(&KIO_DECODER_STATE, burst[i]);
                        unsigned char key = token.character;
                        if (token.type == KIO_TOKEN_NONE ||
                                token.type == KIO_TOKEN_PASTE_START ||
                                token.type == KIO_TOKEN_PASTE_END) continue;
                        if (token.type == KIO_TOKEN_WHEEL) {
                                int delta = token.wheel * 3;
                                if (wheel > 100000 || wheel < -100000)
                                        kio_flush_wheel(&wheel);
                                wheel += delta;
                                continue;
                        }
                        kio_flush_wheel(&wheel);
                        switch (token.type) {
                                case KIO_TOKEN_UP:
                                        if (!history) {
                                                memcpy(data->cmd_history[0], data->cmd_input, (size_t)len);
                                                data->cmd_history[0][len] = 0;
                                        }
                                        if (!data->cmd_history_count) {
                                                if (!boundary_notified) {
                                                        command_history_notice(data, "< No command history is available.", cursor);
                                                        boundary_notified = ISTRUE;
                                                }
                                        } else if (history < data->cmd_history_count) {
                                                command_history_load(data, ++history, &len, &cursor);
                                                boundary_notified = ISFALSE;
                                        } else if (!boundary_notified) {
                                                command_history_notice(data, "< End of command history.", cursor);
                                                boundary_notified = ISTRUE;
                                        }
                                        continue;
                                case KIO_TOKEN_DOWN:
                                        if (history) {
                                                command_history_load(data, --history, &len, &cursor);
                                                boundary_notified = ISFALSE;
                                        }
                                        continue;
                                case KIO_TOKEN_LEFT: if (cursor) cursor--; continue;
                                case KIO_TOKEN_RIGHT: if (cursor < len) cursor++; continue;
                                case KIO_TOKEN_HOME: cursor = 0; continue;
                                case KIO_TOKEN_END: cursor = len; continue;
                                case KIO_TOKEN_DELETE:
                                        if (cursor < len) {
                                                memmove(data->cmd_input + cursor, data->cmd_input + cursor + 1,
                                                        (size_t)(len - cursor));
                                                len--;
                                        }
                                        continue;
                                case KIO_TOKEN_CHAR: break;
                                default: continue;
                        }
                        if (key == '\r') key = '\n';
                        if (key == '\n') {
                                command_history_store(data, data->cmd_input, (size_t)len);
                                kio_keep_input(burst + i + 1, (size_t)(n - i - 1));
                                if (len < INPUT_BLOCK - 1) data->cmd_input[len++] = '\n';
                                data->cmd_input[len] = 0;
                                kui_input_end();
                                return len;
                        }
                        if (key == 127 || key == 8) {
                                if (cursor) {
                                        memmove(data->cmd_input + cursor - 1, data->cmd_input + cursor,
                                                (size_t)(len - cursor + 1));
                                        cursor--; len--;
                                }
                                continue;
                        }
                        if (key == 21) {
                                memset(data->cmd_input, 0, INPUT_BLOCK);
                                cursor = len = 0;
                                continue;
                        }
                        if (key == '\t' && KIO_DECODER_STATE.paste) key = ' ';
                        if (key >= 32 && key < 127) {
                                if (len < INPUT_BLOCK - 2) {
                                        memmove(data->cmd_input + cursor + 1, data->cmd_input + cursor,
                                                (size_t)(len - cursor + 1));
                                        data->cmd_input[cursor++] = key;
                                        len++;
                                } else {
                                        kui_input_bell();
                                }
                        }
                }
                kio_flush_wheel(&wheel);
                kui_input_update((char *)data->prompt, (char *)data->cmd_input, (unsigned int)cursor);
        }
}
