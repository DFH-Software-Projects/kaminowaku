// Copyright 2026 Jamison A. Drapeau
#include "cmd_scan.h"
#include "kui.h"

#include <string.h>
#include <termios.h>
#include <unistd.h>

static struct termios CMD_GUARD_TERMIOS;
static int8_t CMD_GUARD_INPUT_LOCKED = ISFALSE;
static int8_t CMD_GUARD_TERMIOS_VALID = ISFALSE;

static void cmd_guard_input_begin(void) {
        struct termios BLOCKED;

        if (CMD_GUARD_INPUT_LOCKED == ISTRUE) return;

        CMD_GUARD_INPUT_LOCKED = ISTRUE;
        CMD_GUARD_TERMIOS_VALID = ISFALSE;
        kui_processing_begin();

        if (!isatty(STDIN_FILENO)) return;
        if (tcgetattr(STDIN_FILENO, &CMD_GUARD_TERMIOS) != NORMAL) return;

        BLOCKED = CMD_GUARD_TERMIOS;
        BLOCKED.c_lflag &= ~(ECHO | ICANON);
        BLOCKED.c_cc[VMIN] = 0;
        BLOCKED.c_cc[VTIME] = 0;

        if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &BLOCKED) == NORMAL) {
                CMD_GUARD_TERMIOS_VALID = ISTRUE;
        }
}

static void cmd_guard_input_end(void) {
        if (CMD_GUARD_INPUT_LOCKED != ISTRUE) return;

        if (CMD_GUARD_TERMIOS_VALID == ISTRUE && isatty(STDIN_FILENO)) {
                (void)tcflush(STDIN_FILENO, TCIFLUSH);
                (void)tcsetattr(STDIN_FILENO, TCSAFLUSH, &CMD_GUARD_TERMIOS);
        }

        CMD_GUARD_TERMIOS_VALID = ISFALSE;
        CMD_GUARD_INPUT_LOCKED = ISFALSE;
        kui_processing_end();
}

static int8_t cmd_guard_multi_scan_requested(const _carry_forward * _prog_data) {
        if (
                !_prog_data
                || !_prog_data->cmd_tokens
                || !_prog_data->active_project_flower
                || !_prog_data->active_project_flower->NEXT
                || _prog_data->active_project_active_target_context == ISTRUE
        ) {
                return ISFALSE;
        }
        return ISTRUE;
}

static int8_t cmd_guard_multi_ping_requested(const _carry_forward * _prog_data) {
        if (
                !_prog_data
                || !_prog_data->cmd_tokens
                || _prog_data->cmd_tokens_count != 2
                || !_prog_data->cmd_tokens[1]
                || !_prog_data->active_project_flower
                || !_prog_data->active_project_flower->NEXT
        ) {
                return ISFALSE;
        }

        return (
                strcmp((const char *)_prog_data->cmd_tokens[1], "-4") == MATCH
                || strcmp((const char *)_prog_data->cmd_tokens[1], "-6") == MATCH
        ) ? ISTRUE : ISFALSE;
}

// @@ Prevent empty/failed tokenization from entering dispatch and host v2 display hooks.
void cmd_scan_safe(_carry_forward * _prog_data) {
        f_type_t BEFORE;
        int8_t GUARD_INPUT;

        if (
                !_prog_data
                || !_prog_data->cmd_tokens
                || _prog_data->cmd_tokens_count == 0
                || !_prog_data->cmd_tokens[0]
        ) {
                return;
        }

        BEFORE = _prog_data->f_type;
        GUARD_INPUT = (BEFORE == C_TOOL_CONTEXTUAL) ? ISFALSE : ISTRUE;

        if (GUARD_INPUT == ISTRUE) cmd_guard_input_begin();

        cmd_scan(_prog_data);

        if (
                BEFORE == S_DEFAULT
                && (
                        (
                                _prog_data->f_type == C_PING
                                && cmd_guard_multi_ping_requested(_prog_data) == ISTRUE
                        )
                        || (
                                _prog_data->f_type == C_SCAN
                                && cmd_guard_multi_scan_requested(_prog_data) == ISTRUE
                        )
                )
        ) {
                kui_progress_begin(_prog_data);
        }

        if (
                (BEFORE == C_PING || BEFORE == C_SCAN)
                && _prog_data->f_type <= S_DEFAULT
        ) {
                kui_progress_end();
        }

        if (
                GUARD_INPUT == ISTRUE
                && _prog_data->f_type <= S_DEFAULT
        ) {
                cmd_guard_input_end();
        }
}
