// Copyright 2026 Jamison A. Drapeau
#include "book_exec.h"
#include "book_audit.h"
#include "book_runtime.h"
#include "kui.h"
#include <stdio.h>
#include <string.h>

// @@ General native Book executor.
//
// Execution responsibilities stop at the runtime boundary. BOOK_SESSION owns
// transport/capture state and books_run_one() owns finalization plus durable
// output commit/discard. This function therefore:
//      1) parses the resolved .lua Book;
//      2) executes it inside one BOOK_RUNTIME;
//      3) preserves stronger termination states set by native code;
//      4) marks ordinary success COMPLETE;
//      5) maps ordinary parser/runtime failure to RUNTIME_ERROR.
book_exec_status_t books_execute(
        BOOK_SESSION * session,
        const unsigned char * book_path
) {
        BOOK_PROGRAM program;
        BOOK_RUNTIME * runtime = NULL;
        BOOK_MULTI_VALUE returns;
        char detail[BOOK_RUNTIME_ERROR_BLOCK + 96U];

        if (!session || !book_path) return BOOK_EXEC_ABNORMAL;

        memset(&program, 0x00, sizeof(program));
        memset(&returns, 0x00, sizeof(returns));
        memset(detail, 0x00, sizeof(detail));

        if (book_parser_parse_file(book_path, &program) != NORMAL) {
                if (session->termination == BOOK_TERM_NONE) {
                        books_session_set_termination(
                                session,
                                BOOK_TERM_RUNTIME_ERROR
                        );
                }

                (void)snprintf(
                        detail,
                        sizeof(detail),
                        "status=parse_error line=%u column=%u detail=%s",
                        program.error_line,
                        program.error_column,
                        program.error[0] ? program.error : "invalid Lua source"
                );
                books_audit_action(session, "RUNTIME", detail);

                kui_add_line(
                        NOTICE_ERROR "Book parser error @ "
                        ANSI_COLOR_CYAN "%u:%u" ANSI_COLOR_RESET
                        ": %s",
                        program.error_line,
                        program.error_column,
                        program.error[0] ? program.error : "invalid Lua source"
                );

                book_program_free(&program);
                return BOOK_EXEC_ABNORMAL;
        }

        session->state = BOOK_SESSION_RUNTIME;

        // !! DEBUG LINES
        if (
                session->prog_data
                && session->prog_data->debug_flag == ISTRUE
        ) {
                kui_add_line_and_render(
                        "!["
                        ANSI_COLOR_MAGENTA
                        "books_execute()"
                        ANSI_COLOR_RESET
                        "]> Parsed "
                        ANSI_COLOR_CYAN
                        "%s"
                        ANSI_COLOR_RESET
                        ": "
                        ANSI_COLOR_CYAN
                        "%lu"
                        ANSI_COLOR_RESET
                        " tokens, "
                        ANSI_COLOR_CYAN
                        "%u"
                        ANSI_COLOR_RESET
                        " AST nodes."
                        , session->book_name
                        , (unsigned long)program.lexer.count
                        , program.node_count
                );
        }

        runtime = book_runtime_create(&program, session);
        if (!runtime) {
                if (session->termination == BOOK_TERM_NONE) {
                        books_session_set_termination(
                                session,
                                BOOK_TERM_RUNTIME_ERROR
                        );
                }

                books_audit_action(
                        session,
                        "RUNTIME",
                        "status=create_failed"
                );
                kui_add_line(
                        NOTICE_ERROR "Failed to create Book Lua runtime."
                );

                book_program_free(&program);
                return BOOK_EXEC_ABNORMAL;
        }

        if (book_runtime_execute(runtime, &returns) != NORMAL) {
                const char * error = book_runtime_error(runtime);
                unsigned int line = book_runtime_error_line(runtime);
                unsigned int column = book_runtime_error_column(runtime);

                // Native lifecycle code may already have selected a stronger,
                // semantically meaningful state such as BAILED or LIMIT_ERROR.
                // Never flatten those states into a generic runtime failure.
                if (session->termination == BOOK_TERM_NONE) {
                        books_session_set_termination(
                                session,
                                BOOK_TERM_RUNTIME_ERROR
                        );
                }

                (void)snprintf(
                        detail,
                        sizeof(detail),
                        "status=runtime_error line=%u column=%u detail=%s",
                        line,
                        column,
                        error && error[0] ? error : "Lua runtime failure"
                );
                books_audit_action(session, "RUNTIME", detail);

                if (session->termination != BOOK_TERM_BAILED) {
                        kui_add_line(
                                NOTICE_ERROR "Book runtime error @ "
                                ANSI_COLOR_CYAN "%u:%u" ANSI_COLOR_RESET
                                ": %s",
                                line,
                                column,
                                error && error[0]
                                        ? error
                                        : "Lua runtime failure"
                        );
                }

                book_runtime_destroy(runtime);
                book_program_free(&program);
                return BOOK_EXEC_ABNORMAL;
        }

        // Top-level return values are intentionally ignored. Durable Book data
        // is explicit through kami.result.emit(); successful zero-result Books
        // are valid and replace the prior .out with an empty current-state file.
        //
        // A native path may already have selected a stronger termination while
        // returning a status value to Lua (for example a capture/limit failure).
        // Reaching the end of Lua source must never erase that native state.
        if (session->termination != BOOK_TERM_NONE) {
                (void)snprintf(
                        detail,
                        sizeof(detail),
                        "status=native_termination termination=%s",
                        books_termination_name(session->termination)
                );
                books_audit_action(session, "RUNTIME", detail);

                book_runtime_destroy(runtime);
                book_program_free(&program);
                return BOOK_EXEC_ABNORMAL;
        }

        // !! DEBUG LINES
        if (
                session->prog_data
                && session->prog_data->debug_flag == ISTRUE
        ) {
                kui_add_line_and_render(
                        "!["
                        ANSI_COLOR_MAGENTA
                        "books_execute()"
                        ANSI_COLOR_RESET
                        "]> Instructions = "
                        ANSI_COLOR_CYAN
                        "%llu"
                        ANSI_COLOR_RESET
                        ", memory = "
                        ANSI_COLOR_CYAN
                        "%lu"
                        ANSI_COLOR_RESET
                        " bytes."
                        , (unsigned long long)book_runtime_instruction_count(runtime)
                        , (unsigned long)book_runtime_memory_used(runtime)
                );
        }

        books_audit_action(
                session,
                "RUNTIME",
                "status=complete"
        );
        books_session_set_termination(
                session,
                BOOK_TERM_COMPLETE
        );

        book_runtime_destroy(runtime);
        book_program_free(&program);
        return BOOK_EXEC_NORMAL;
}
