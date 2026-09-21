// Copyright 2026 Jamison A. Drapeau
#ifndef __BOOK_MODULE__H
#define __BOOK_MODULE__H

#include "book_runtime.h"

#define BOOK_MODULE_LIMIT        128U
#define BOOK_MODULE_DEPTH_LIMIT  32U
#define BOOK_MODULE_NAME_BLOCK   256U
#define BOOK_MODULE_PATH_BLOCK   1024U
#define BOOK_MODULE_SOURCE_LIMIT (4U * 1024U * 1024U)

int book_modules_install(BOOK_RUNTIME * runtime);
void book_modules_destroy(BOOK_RUNTIME * runtime);

// @@ Private C acceptance/diagnostic entry point. Normal Lua uses require().
int book_module_require(
        BOOK_RUNTIME * runtime,
        const unsigned char * name,
        c_size_t name_length,
        BOOK_VALUE * value
);

#endif
