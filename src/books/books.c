// Copyright 2026 Jamison A. Drapeau
#include "books.h"
#include "book_session.h"
#include "book_exec.h"
#include "book_output.h"
#include "book_persist.h"
#include "book_audit.h"
#include "tlib.h"
#include "kui.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

void books_print_usage(void) {
        kui_add_line("< Usage: book [ " ANSI_COLOR_CYAN "<book-name>" ANSI_COLOR_RESET " ]");
        return;
}

static int books_regular_file(const unsigned char * book_path) {
        struct stat info;

        if (!book_path) return ISFALSE;
        if (lstat((const char*)book_path, &info) != NORMAL) return ISFALSE;
        if (S_ISLNK(info.st_mode) || !S_ISREG(info.st_mode)) return ISFALSE;
        return ISTRUE;
}


typedef struct BOOK_LIST_ENTRY {
        unsigned char   name[BOOK_NAME_BLOCK];
        book_location_t location;
} BOOK_LIST_ENTRY;

static int books_list_compare(const void * left, const void * right) {
        const BOOK_LIST_ENTRY * a = (const BOOK_LIST_ENTRY*)left;
        const BOOK_LIST_ENTRY * b = (const BOOK_LIST_ENTRY*)right;

        return strcmp((const char*)a->name, (const char*)b->name);
}

static int books_list_append(
        BOOK_LIST_ENTRY ** entries,
        c_size_t * count,
        c_size_t * capacity,
        const unsigned char * name,
        book_location_t location
) {
        BOOK_LIST_ENTRY * resized;

        if (!entries || !count || !capacity || !name) return ABNORMAL;

        for (c_size_t i = 0; i < *count; i++) {
                if (strcmp((const char*)(*entries)[i].name, (const char*)name) == MATCH) {
                        return NORMAL;
                }
        }

        if (*count == *capacity) {
                c_size_t next_capacity = *capacity == 0 ? 8 : *capacity * 2;

                resized = realloc(*entries, next_capacity * sizeof(**entries));
                if (!resized) return ABNORMAL;

                *entries = resized;
                *capacity = next_capacity;
        }

        memset(&(*entries)[*count], 0x00, sizeof(**entries));
        snprintf(
                (char*)(*entries)[*count].name,
                sizeof((*entries)[*count].name),
                "%s",
                name
        );
        (*entries)[*count].location = location;
        (*count)++;
        return NORMAL;
}

static int books_list_scan_directory(
        const char * directory_path,
        book_location_t location,
        BOOK_LIST_ENTRY ** entries,
        c_size_t * count,
        c_size_t * capacity
) {
        DIR * directory;
        struct dirent * entry;

        if (!directory_path || !entries || !count || !capacity) return ABNORMAL;

        directory = opendir(directory_path);
        if (!directory) return NORMAL;

        while ((entry = readdir(directory)) != NULL) {
                unsigned char name[BOOK_NAME_BLOCK];
                unsigned char path[MAX_PATH];
                c_size_t length;
                c_size_t name_length;
                int written;

                length = (c_size_t)strlen(entry->d_name);
                if (length <= 4 || strcmp(entry->d_name + length - 4, ".lua") != MATCH) continue;

                name_length = length - 4;
                if (name_length == 0 || name_length >= sizeof(name)) continue;

                memset(name, 0x00, sizeof(name));
                memcpy(name, entry->d_name, name_length);
                if (book_name_validate(name) != NORMAL) continue;

                memset(path, 0x00, sizeof(path));
                written = snprintf(
                        (char*)path,
                        sizeof(path),
                        "%s%s",
                        directory_path,
                        entry->d_name
                );
                if (written <= 0 || (c_size_t)written >= sizeof(path)) continue;
                if (books_regular_file(path) != ISTRUE) continue;

                if (
                        books_list_append(
                                entries,
                                count,
                                capacity,
                                name,
                                location
                        ) != NORMAL
                ) {
                        closedir(directory);
                        return ABNORMAL;
                }
        }

        closedir(directory);
        return NORMAL;
}

static void books_list_available(void) {
        BOOK_LIST_ENTRY * entries = NULL;
        c_size_t count = 0;
        c_size_t capacity = 0;

        if (
                books_list_scan_directory(
                        KAMI_SYSTEM_BOOKS_DIR,
                        BOOK_SYSTEM,
                        &entries,
                        &count,
                        &capacity
                ) != NORMAL
                || books_list_scan_directory(
                        KAMI_USER_BOOKS_DIR,
                        BOOK_USER,
                        &entries,
                        &count,
                        &capacity
                ) != NORMAL
        ) {
                free(entries);
                kui_add_line(NOTICE_ERROR "Failed to enumerate available books.");
                return;
        }

        if (count == 0) {
                free(entries);
                kui_add_line("< No books available.");
                return;
        }

        qsort(entries, count, sizeof(*entries), books_list_compare);

        c_size_t name_width = 0;
        for (c_size_t i = 0; i < count; i++) {
                c_size_t length = (c_size_t)strlen(
                        (const char*)entries[i].name
                );

                if (length > name_width) {
                        name_width = length;
                }
        }

        kui_add_line("< Available books:");
        for (c_size_t i = 0; i < count; i++) {
                kui_add_line(
                        "    " ANSI_COLOR_CYAN "%-*s" ANSI_COLOR_RESET "    %s",
                        (int)name_width,
                        (char*)entries[i].name,
                        entries[i].location == BOOK_SYSTEM ? "system" : "user"
                );
        }

        free(entries);
        return;
}

book_location_t books_resolve(
        const unsigned char * book_name,
        unsigned char * book_path,
        c_size_t book_path_size
) {
        int written;

        if (!book_name || !book_path || book_path_size == 0) return BOOK_NOT_FOUND;
        if (book_name_validate(book_name) != NORMAL) return BOOK_NOT_FOUND;

        memset(book_path, 0x00, book_path_size);
        written = snprintf(
                (char*)book_path,
                book_path_size,
                "%s%s.lua",
                KAMI_SYSTEM_BOOKS_DIR,
                book_name
        );
        if (written > 0 && (c_size_t)written < book_path_size) {
                if (books_regular_file(book_path) == ISTRUE) return BOOK_SYSTEM;
        }

        memset(book_path, 0x00, book_path_size);
        written = snprintf(
                (char*)book_path,
                book_path_size,
                "%s%s.lua",
                KAMI_USER_BOOKS_DIR,
                book_name
        );
        if (written > 0 && (c_size_t)written < book_path_size) {
                if (books_regular_file(book_path) == ISTRUE) return BOOK_USER;
        }

        memset(book_path, 0x00, book_path_size);
        return BOOK_NOT_FOUND;
}

static int books_target_directory_from_data_path(
        const char * data_path,
        char * target_directory,
        c_size_t target_directory_size
) {
        c_size_t length;

        if (!data_path || !target_directory || target_directory_size == 0) return ABNORMAL;
        length = (c_size_t)strlen(data_path);
        if (length <= 6 || strcmp(data_path + length - 6, "/.data") != MATCH) return ABNORMAL;
        if (length - 6 >= target_directory_size) return ABNORMAL;

        memset(target_directory, 0x00, target_directory_size);
        memcpy(target_directory, data_path, length - 6);
        return NORMAL;
}

// @@ Record Book execution as target/project scan activity.
static int books_mark_target_scanned(
        _carry_forward * _prog_data,
        FLOWER * target,
        uint64_t scan_time_ns
) {
        char petal_path[MAX_PATH];
        TARGET_SCAN_DATA previous_scan;

        if (
                !_prog_data
                || !target
                || !target->PETAL
                || scan_time_ns == 0
        ) {
                return ABNORMAL;
        }

        memcpy(
                &previous_scan,
                &target->PETAL->SCAN,
                sizeof(previous_scan)
        );

        if (
                target->PETAL->SCAN.SCAN_VERSION
                != TARGET_SCAN_DATA_VERSION
        ) {
                memset(
                        &target->PETAL->SCAN,
                        0x00,
                        sizeof(target->PETAL->SCAN)
                );
                target->PETAL->SCAN.SCAN_VERSION =
                        TARGET_SCAN_DATA_VERSION;
        }

        target->PETAL->SCAN.LAST_SCAN_NS = scan_time_ns;

        memset(petal_path, 0x00, sizeof(petal_path));
        if (
                build_petal_path(
                        petal_path,
                        sizeof(petal_path),
                        _prog_data->wd,
                        _prog_data->active_project,
                        target->TID
                ) != NORMAL
                || targets_write_petal_data(
                        petal_path,
                        target->PETAL
                ) != NORMAL
        ) {
                memcpy(
                        &target->PETAL->SCAN,
                        &previous_scan,
                        sizeof(previous_scan)
                );
                return ABNORMAL;
        }

        _prog_data->active_project_has_been_scanned = ISTRUE;

        if (scan_time_ns > _prog_data->active_project_last_scan_ns) {
                _prog_data->active_project_last_scan_ns = scan_time_ns;
        }

        return NORMAL;
}

static int books_run_one(
        _carry_forward * _prog_data,
        const unsigned char * book_name,
        const unsigned char * book_path,
        FLOWER * target,
        const char * target_directory
) {
        BOOK_SESSION session;
        book_exec_status_t execution_status;
        c_size_t staged_count;

        if (!_prog_data || !book_name || !book_path || !target || !target->PETAL || !target_directory) {
                return ABNORMAL;
        }

        memset(&session, 0x00, sizeof(session));
        if (
                books_session_prepare(
                        &session,
                        _prog_data,
                        book_name,
                        target,
                        target_directory
                ) != NORMAL
        ) {
                kui_add_line(
                        NOTICE_ERROR "Failed to establish book capture session for "
                        ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET ".",
                        target->TID
                );
                kui_add_line(NOTICE_INFO "No book-generated network action was permitted.");
                return ABNORMAL;
        }

        if (books_session_network_allowed(&session) != ISTRUE) {
                books_session_set_termination(&session, BOOK_TERM_CAPTURE_SETUP_ERROR);
                books_session_finalize(&session);
                kui_add_line(NOTICE_ERROR "Book capture gate failed closed.");
                return ABNORMAL;
        }

        kui_add_line(
                NOTICE_SUCCESS "Book session " ANSI_COLOR_CYAN "%llu" ANSI_COLOR_RESET
                " capture active for " ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET ".",
                (unsigned long long)session.session_id,
                target->TID
        );
        kui_add_line(
                NOTICE_INFO "PCAP: " ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET,
                session.pcap_path
        );

        execution_status = books_execute(&session, book_path);
        staged_count = book_output_count(&session);

        // A successful networked book is not complete unless evidence exists.
        // Drain once more while the transport is still alive so the eventual
        // BOOK_END status cannot claim COMPLETE for a header-only PCAP.
        if (
                session.termination == BOOK_TERM_COMPLETE
                && session.network_actions > 0
        ) {
                int evidence_status = books_session_capture_drain(&session, 50);

                if (evidence_status != NORMAL || session.pcap_packets == 0) {
                        if (session.termination == BOOK_TERM_COMPLETE) {
                                books_session_set_termination(
                                        &session,
                                        BOOK_TERM_CAPTURE_SETUP_ERROR
                                );
                        }
                        books_audit_action(
                                &session,
                                "ERROR",
                                session.pcap_packets == 0
                                        ? "reason=evidence_empty"
                                        : "reason=evidence_finalize"
                        );
                        kui_add_line(
                                NOTICE_ERROR "Book produced network activity without valid packet evidence. "
                                "Output will not be committed."
                        );
                }
        }

        // Transport teardown and packet capture finalization must precede state commit.
        books_session_finalize(&session);

        // @@ LAST_SCAN_NS follows the built-in scanner's completion-time semantics.
        // Every Book invocation that reached an active capture session counts as
        // scan activity, including BAILED/error runs. Output persistence remains
        // independent and still follows the termination rules below.
        if (
                books_mark_target_scanned(
                        _prog_data,
                        target,
                        session.ended_ns
                ) != NORMAL
        ) {
                books_audit_action(
                        &session,
                        "ERROR",
                        "reason=scan_timestamp"
                );
                kui_add_line(
                        NOTICE_ERROR
                        "Failed to persist Book scan timestamp for "
                        ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET ".",
                        target->TID
                );

                // A COMPLETE Book must not publish new current-state output when
                // the corresponding target scan metadata could not be persisted.
                if (session.termination == BOOK_TERM_COMPLETE) {
                        session.termination = BOOK_TERM_OUTPUT_COMMIT_ERROR;
                }
        }

        if (session.termination == BOOK_TERM_COMPLETE) {
                if (book_output_commit(&session, target_directory) != NORMAL) {
                        session.termination = BOOK_TERM_OUTPUT_COMMIT_ERROR;
                        books_audit_action(&session, "ERROR", "reason=output_commit");
                        kui_add_line(
                                NOTICE_ERROR "Book completed but " ANSI_COLOR_CYAN "%s.out" ANSI_COLOR_RESET
                                " could not be committed. Previous output was preserved.",
                                session.book_name
                        );
                        book_output_discard(&session);
                        return ABNORMAL;
                }

                books_audit_action(&session, "OUTPUT_COMMIT", NULL);
                kui_add_line(
                        NOTICE_SUCCESS "Book " ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET
                        " completed with " ANSI_COLOR_CYAN "%lu" ANSI_COLOR_RESET " result record%s.",
                        session.book_name,
                        (unsigned long)staged_count,
                        staged_count == 1 ? "" : "s"
                );
                book_output_discard(&session);
                return NORMAL;
        }

        book_output_discard(&session);

        if (session.termination == BOOK_TERM_BAILED) {
                kui_add_line(NOTICE_INFO "Book output was not committed; PCAP retained.");
                return NORMAL;
        }

        if (execution_status != BOOK_EXEC_NORMAL || session.termination != BOOK_TERM_COMPLETE) {
                kui_add_line(
                        NOTICE_WARNING "Book terminated with " ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET
                        ". Previous output preserved; PCAP retained.",
                        books_termination_name(session.termination)
                );
        }
        return ABNORMAL;
}

static void books_run_project(
        _carry_forward * _prog_data,
        const unsigned char * book_name,
        const unsigned char * book_path
) {
        FLOWER * current;
        uint64_t complete = 0;
        uint64_t failed = 0;

        if (!_prog_data || !_prog_data->active_project_flower) return;

        current = _prog_data->active_project_flower->NEXT;
        if (!current) {
                kui_add_line(NOTICE_WARNING "No targets available for project book execution.");
                return;
        }

        while (current) {
                char data_path[MAX_PATH];
                char target_directory[MAX_PATH];
                TARGET petal;
                FLOWER target;

                memset(data_path, 0x00, sizeof(data_path));
                memset(target_directory, 0x00, sizeof(target_directory));
                memset(&petal, 0x00, sizeof(petal));
                memset(&target, 0x00, sizeof(target));

                if (
                        build_petal_path(
                                data_path,
                                sizeof(data_path),
                                _prog_data->wd,
                                _prog_data->active_project,
                                current->TID
                        ) != NORMAL
                        || targets_read_petal_data(data_path, &petal) != NORMAL
                        || books_target_directory_from_data_path(
                                data_path,
                                target_directory,
                                sizeof(target_directory)
                        ) != NORMAL
                ) {
                        kui_add_line(
                                NOTICE_WARNING "Skipping " ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET
                                ": target state could not be loaded.",
                                current->TID
                        );
                        failed++;
                        current = current->NEXT;
                        continue;
                }

                memcpy(target.TID, current->TID, TID_BLOCK);
                target.PETAL = &petal;

                if (
                        books_run_one(
                                _prog_data,
                                book_name,
                                book_path,
                                &target,
                                target_directory
                        ) == NORMAL
                ) complete++;
                else failed++;

                current = current->NEXT;
        }

        kui_add_line(
                NOTICE_INFO "Book project run finished: "
                ANSI_COLOR_CYAN "%llu" ANSI_COLOR_RESET " completed, "
                ANSI_COLOR_CYAN "%llu" ANSI_COLOR_RESET " failed/skipped.",
                (unsigned long long)complete,
                (unsigned long long)failed
        );
}

void books_try_run(_carry_forward * _prog_data) {
        unsigned char book_path[MAX_PATH];
        book_location_t location;

        if (!_prog_data) return;

        if (_prog_data->cmd_tokens_count == 1 || !_prog_data->cmd_tokens[1]) {
                books_list_available();
                return;
        }

        if (_prog_data->cmd_tokens_count != 2) {
                books_print_usage();
                return;
        }

        if (_prog_data->active_project[0] == 0x00) {
                kui_add_line("< This command requires an active project to be loaded.");
                return;
        }

        if (book_name_validate(_prog_data->cmd_tokens[1]) != NORMAL) {
                kui_add_line("< Invalid book name.");
                return;
        }

        memset(book_path, 0x00, sizeof(book_path));
        location = books_resolve(
                _prog_data->cmd_tokens[1],
                book_path,
                sizeof(book_path)
        );

        if (location == BOOK_NOT_FOUND) {
                kui_add_line(
                        "< Book " ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET " not found.",
                        _prog_data->cmd_tokens[1]
                );
                return;
        }

        if (
                _prog_data->active_project_active_target_context == ISTRUE
                && _prog_data->active_project_active_target
                && _prog_data->active_project_active_target->PETAL
                && _prog_data->active_project_active_target_directory
        ) {
                (void)books_run_one(
                        _prog_data,
                        _prog_data->cmd_tokens[1],
                        book_path,
                        _prog_data->active_project_active_target,
                        _prog_data->active_project_active_target_directory
                );
                return;
        }

        books_run_project(
                _prog_data,
                _prog_data->cmd_tokens[1],
                book_path
        );
        return;
}
