// Copyright 2026 Jamison A. Drapeau
#ifndef __BOOK_NATIVE__H
#define __BOOK_NATIVE__H

#include "book_runtime.h"

#define BOOK_NATIVE_IO_LIMIT (1024U * 1024U)

int book_native_install(BOOK_RUNTIME * runtime);
void book_native_destroy(BOOK_RUNTIME * runtime);

// @@ The private table is injected only into trusted kami.* module scopes.
int book_native_private_table(
        BOOK_RUNTIME * runtime,
        BOOK_VALUE * value
);

#endif
