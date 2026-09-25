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

#ifndef TAB_STOP
#define TAB_STOP 8
#endif

/* ================== Terminal mode ================== */

void disable_raw_mode(void) {
        tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
}

void enable_raw_mode(void) {
        tcgetattr(STDIN_FILENO, &orig_termios);
        atexit(disable_raw_mode);

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

int read_line(_carry_forward *_prog_data) {
        int length_marker = 0;
        int cursor = 0;
        unsigned int history_index = 0;
        int8_t history_boundary_notified = ISFALSE;

        memset(_prog_data->cmd_input, 0x00, INPUT_BLOCK);
        memset(_prog_data->cmd_history[0], 0x00, INPUT_BLOCK);

        /* @@ KUI owns both the prompt anchor and all terminal writes. */
        kui_input_begin();
        kui_input_update((char *)_prog_data->prompt, (char *)_prog_data->cmd_input, (unsigned int)cursor);

        for (;;) {
                
                // Temporary data store
                unsigned char tmp_buffer[BLEN];

                // Flush before reads.
                //tcflush(STDIN_FILENO, TCIFLUSH);
                ssize_t n = read_input_burst(tmp_buffer, sizeof(tmp_buffer));
                
                
                if (n <= 0) {
                        kui_input_end();
                        return -1;
                }

                for (ssize_t i = 0; i < n; i++) {
                        unsigned char key = tmp_buffer[i];

                        // @@ Bread and Butter of IO
                        if (key == '\n') {

                                /* Store the submitted command without its trailing newline. */
                                command_history_store(
                                        _prog_data,
                                        _prog_data->cmd_input,
                                        (size_t)length_marker
                                );

                                /* Discard any trailing paste/bytes from the PTY before next prompt. */
                                drain_input_until_idle(STDIN_FILENO, /*idle_ms=*/60, /*hard_cap=*/1<<20);

                                if (length_marker < INPUT_BLOCK - 1)
                                        _prog_data->cmd_input[length_marker++] = '\n';
                                _prog_data->cmd_input[length_marker] = '\0';
                                kui_input_end();
                                return length_marker;
                        }

                        /* backspace / delete */
                        if (key == 127 || key == 8) {
                                if (cursor > 0) {
                                        memmove(_prog_data->cmd_input + cursor - 1, _prog_data->cmd_input + cursor,
                                                (size_t)(length_marker - cursor));
                                        length_marker--;
                                        cursor--;
                                        _prog_data->cmd_input[length_marker] = '\0';
                                }
                                continue;
                        }

                        /* Ctrl+U — clear entire line */
                        if (key == 21) {  /* ASCII 0x15 = Ctrl+U */
                                if (length_marker > 0) {
                                        
                                        /* Zero out the entire buffer for safety */
                                        memset(_prog_data->cmd_input, 0, INPUT_BLOCK);
                                
                                        /* Reset editing state */
                                        length_marker = 0;
                                        cursor = 0;
                                
                                        /* Immediate visual feedback */
                                        kui_input_update((char *)_prog_data->prompt, (char *)_prog_data->cmd_input, (unsigned int)cursor);
                                }
                                continue;
                        }


                        /* arrow keys, home/end, delete */
                        if (key == '\033') {
                                unsigned char seq[32] = {0};
                                ssize_t remaining = n - i - 1;
                        
                                /* Grab rest of sequence (same style you already use). */
                                if (remaining > 0) {
                                        ssize_t to_copy = (remaining < (ssize_t)(sizeof(seq) - 1))
                                                          ? remaining
                                                          : (ssize_t)(sizeof(seq) - 1);
                                        memcpy(seq, &tmp_buffer[i + 1], (size_t)to_copy);
                                        seq[to_copy] = '\0';
                                } else {
                                        ssize_t m = read(STDIN_FILENO, seq, sizeof(seq) - 1);
                                        if (m > 0) seq[m] = '\0';
                                }
                        
                                if (seq[0] == '[') {

                                        /* ───────────────────────────── */
                                        /*       Vim-style SGR mouse     */
                                        /* ───────────────────────────── */
                                        if (seq[1] == '<') {
                                                int  b = 0, x = 0, y = 0;
                                                char action = 0;
                                        
                                                /* We always try to locate the end of this SGR sequence
                                                 * (the 'M' or 'm') so we ONLY consume ONE mouse event,
                                                 * not the entire remaining burst.
                                                 */
                                                size_t consumed = 1;  /* ESC itself already in 'key' */
                                                char *pM   = strchr((char *)seq, 'M');
                                                char *pm   = strchr((char *)seq, 'm');
                                                char *pend = NULL;
                                        
                                                if (pM && pm)      pend = (pM < pm) ? pM : pm;
                                                else if (pM)       pend = pM;
                                                else if (pm)       pend = pm;
                                        
                                                if (pend) {
                                                        /* '[' is seq[0], so pend - seq gives bytes from '['..'M/m'. */
                                                        consumed += (size_t)(pend - (char *)seq + 1);
                                        
                                                        /* Try to parse "<b;x;yM" from seq[1] (the '<'). */
                                                        if (sscanf((const char *)&seq[1],
                                                                   "<%d;%d;%d%c", &b, &x, &y, &action) == 4) {
                                        
                                                                if (b == 64 && action == 'M') {
                                                                        /* Scroll wheel up */
                                                                        kui_input_scroll(+3);
                                                                } else if (b == 65 && action == 'M') {
                                                                        /* Scroll wheel down */
                                                                        kui_input_scroll(-3);
                                                                }
                                                        }
                                        
                                                        /* In ALL cases (parsed or not), skip exactly this one event. */
                                                        i += (ssize_t)consumed;
                                                        continue;
                                                } else {
                                                        /* Incomplete SGR sequence in this burst (no 'M'/'m' yet).
                                                         * Don't nuke the whole buffer; just consume ESC + '[' + '<'
                                                         * and let later bursts / the stray-[ guard handle leftovers.
                                                         */
                                                        i += 2;   /* ESC (already at i) + '[' (seq[0]); next loop sees the rest */
                                                        continue;
                                                }
                                        }

                                        /* ───────────────────────────── */
                                        /*   LEGACY XTERM MOUSE: ESC[M   */
                                        /*   Format: ESC [ M b x y       */
                                        /* ───────────────────────────── */
                                        if (seq[1] == 'M') {
                                                /* We expect at least: "[Mxyz" (5 bytes: '[','M','b','x','y') */
                                                if (remaining >= 4) {
                                                        unsigned char b = seq[2];
                                                        unsigned char cx = seq[3];
                                                        unsigned char cy = seq[4];

                                                        /* X10/xterm encodes as: 32 + value */
                                                        int btn = (int)(b - 32);
                                                        int mx  = (int)(cx - 32);
                                                        int my  = (int)(cy - 32);

                                                        /* Wheel up/down for xterm: buttons 64/65 (modifiers may add bits). */
                                                        if ((btn & 0xE0) == 0x40) {  /* high bits indicate wheel */
                                                                int wheel = btn & 0x0F;
                                                                if (wheel == 0) {
                                                                        /* wheel up */
                                                                        kui_input_scroll(+3);
                                                                } else if (wheel == 1) {
                                                                        /* wheel down */
                                                                        kui_input_scroll(-3);
                                                                }
                                                        }

                                                        /* Consume ESC + '[' + 'M' + b + x + y = 6 bytes total */
                                                        i += 5;  /* ESC at tmp_buffer[i], so +5 more */
                                                        continue;
                                                }
                                                /* if not enough bytes, just fall through (will be harmless) */
                                        }


                                        unsigned char code = seq[1];
                                        switch (code) {
                                                /*───────────────────────────*/
                                                /*        SCROLLBACK         */
                                                /*───────────────────────────*/
                                                case 'A': { /* Up arrow: older command */
                                                        if (history_index == 0) {
                                                                memset(
                                                                        _prog_data->cmd_history[0],
                                                                        0x00,
                                                                        INPUT_BLOCK
                                                                );
                                                                memcpy(
                                                                        _prog_data->cmd_history[0],
                                                                        _prog_data->cmd_input,
                                                                        (size_t)length_marker
                                                                );
                                                                _prog_data->cmd_history[0][length_marker] = 0x00;
                                                        }
                                                        if (_prog_data->cmd_history_count == 0) {
                                                                if (history_boundary_notified == ISFALSE) {
                                                                        command_history_notice(
                                                                                _prog_data,
                                                                                "< No command history is available.",
                                                                                cursor
                                                                        );
                                                                        history_boundary_notified = ISTRUE;
                                                                }
                                                        
                                                                i += 2;
                                                                continue;
                                                        }
                                                        /*
                                                         * Move backward through available history.
                                                         * Reaching the oldest command does not print a notice.
                                                         */
                                                        if (history_index < _prog_data->cmd_history_count) {
                                                                history_index++;
                                                                command_history_load(
                                                                        _prog_data,
                                                                        history_index,
                                                                        &length_marker,
                                                                        &cursor
                                                                );
                                                        
                                                                history_boundary_notified = ISFALSE;
                                                        
                                                        /*
                                                         * We were already displaying the oldest command.
                                                         * A subsequent Up-arrow prints the boundary notice.
                                                         */
                                                        } else if (history_boundary_notified == ISFALSE) {
                                                                command_history_notice(
                                                                        _prog_data,
                                                                        "< End of command history.",
                                                                        cursor
                                                                );
                                                                history_boundary_notified = ISTRUE;
                                                        }
                                                        i += 2;
                                                        break;
                                                }
                                                case 'B': { /* Down arrow: newer command/current draft */
                                                        if (history_index == 0) {
                                                                i += 2;
                                                                continue;
                                                        }

                                                        history_index--;
                                                        command_history_load(
                                                                _prog_data,
                                                                history_index,
                                                                &length_marker,
                                                                &cursor
                                                        );
                                                        history_boundary_notified = ISFALSE;
                                                        i += 2;
                                                        break;
                                                }
                                                case 'D': /* Left */
                                                        if (cursor > 0) cursor--;
                                                        i += 2;
                                                        break;
                                                case 'C': /* Right */
                                                        if (cursor < length_marker) cursor++;
                                                        i += 2;
                                                        break;
                                                case 'H': /* Home */
                                                        cursor = 0;
                                                        i += 2;
                                                        break;
                                                case 'F': /* End */
                                                        cursor = length_marker;
                                                        i += 2;
                                                        break;
                                                case '3': /* Delete (ESC [ 3 ~) */
                                                        if (seq[2] == '~') {
                                                                if (cursor < length_marker) {
                                                                        memmove(_prog_data->cmd_input + cursor,
                                                                                _prog_data->cmd_input + cursor + 1,
                                                                                (size_t)(length_marker - cursor - 1));
                                                                        length_marker--;
                                                                        _prog_data->cmd_input[length_marker] = '\0';
                                                                }
                                                                /* Always consume the full sequence */
                                                                i += 3;
                                                        }
                                                        break;
                                                case '1': case '7':
                                                case '4': case '8':
                                                        if (seq[2] == '~') {
                                                                if (code == '1' || code == '7')
                                                                        cursor = 0;
                                                                if (code == '4' || code == '8')
                                                                        cursor = length_marker;
                                                                i += 3;
                                                        }
                                                        break;
                                                default:
                                                        break;
                                        }
                                }
                                continue;
                        }

                        /* Swallow stray mouse fragments that started without ESC.
                         * e.g. "[<64;61;71M" or "[Mxyz" when ESC was in a previous burst.
                         */
                        if (key == '[') {
                                ssize_t j = i + 1;

                                if (j < n && (tmp_buffer[j] == '<' || tmp_buffer[j] == 'M')) {
                                        /* Heuristic:
                                         *   - "[<"  → SGR-style mouse tail
                                         *   - "[M"  → legacy xterm mouse tail
                                         * Consume until 'M' or 'm' (inclusive) in this burst.
                                         */
                                        while (j < n && tmp_buffer[j] != 'M' && tmp_buffer[j] != 'm') {
                                                j++;
                                        }
                                        if (j < n) {
                                                j++;    /* include the terminating M/m */
                                        }

                                        /* We've now eaten "[<...M" or "[M...".
                                         * Set i so that the for-loop's i++ lands on the next byte.
                                         */
                                        i = j - 1;
                                        continue;
                                }
                        }


                        /* printable ASCII */
                        if (key >= 32 && key < 127) {
                                if (length_marker < INPUT_BLOCK - 1) {
                                        memmove(_prog_data->cmd_input + cursor + 1, _prog_data->cmd_input + cursor,
                                                (size_t)(length_marker - cursor));
                                        _prog_data->cmd_input[cursor] = (char)key;
                                        length_marker++;
                                        cursor++;
                                        _prog_data->cmd_input[length_marker] = '\0';
                                } else {
                                        kui_input_bell();
                                }
                        }
                }

                kui_input_update((char *)_prog_data->prompt, (char *)_prog_data->cmd_input, (unsigned int)cursor);
        }
}