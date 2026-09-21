// Copyright 2026 Jamison A. Drapeau
#include "book_audit.h"
#include "helpers.h"
#include <inttypes.h>
#include <stdio.h>

// @@ High-level book API audit event. Payload bytes stay in the PCAP, not the log.
void books_audit_action(
        BOOK_SESSION * session,
        const char * event,
        const char * detail
) {
        FILE * log;

        if (!session || !session->prog_data || !event) return;

        log = (FILE*)session->prog_data->log;
        if (!log) return;

        fprintf(
                log,
                "--[%s]--\nBOOK_%s session=%" PRIu64 " book=%s tid=%s%s%s\n",
                timestamp(),
                event,
                session->session_id,
                session->book_name,
                session->tid,
                detail ? " " : "",
                detail ? detail : ""
        );
        fflush(log);
        return;
}
