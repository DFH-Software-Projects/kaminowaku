// Copyright 2026 Jamison A. Drapeau
#ifndef __BOOK_EXEC__H
#define __BOOK_EXEC__H

#include "book_session.h"

// @@ Sole boundary between Book orchestration and the execution implementation.
// Parser/runtime internals must remain behind this interface.
typedef enum {
        BOOK_EXEC_NORMAL   = NORMAL,
        BOOK_EXEC_ABNORMAL = ABNORMAL
} book_exec_status_t;

book_exec_status_t books_execute(
        BOOK_SESSION * session,
        const unsigned char * book_path
);

#endif
