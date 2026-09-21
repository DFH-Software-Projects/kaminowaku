// Copyright 2026 Jamison A. Drapeau
#ifndef __BOOK_PERSIST__H
#define __BOOK_PERSIST__H

#include "book_session.h"

int book_output_commit(
        BOOK_SESSION * session,
        const char * target_directory
);

int book_output_directory_has_output(
        const char * target_directory
);

int book_output_render_directory(
        const char * target_directory
);

#endif
