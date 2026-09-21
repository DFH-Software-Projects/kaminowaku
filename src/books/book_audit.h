// Copyright 2026 Jamison A. Drapeau
#ifndef __BOOK_AUDIT__H
#define __BOOK_AUDIT__H

#include "book_session.h"

void books_audit_action(
        BOOK_SESSION * session,
        const char * event,
        const char * detail
);

#endif
