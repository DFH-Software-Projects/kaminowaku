// Copyright 2026 Jamison A. Drapeau
#include "book_core.h"
#include <string.h>

int book_name_validate(const unsigned char * book_name) {
        c_size_t length;

        if (!book_name) return ABNORMAL;
        length = (c_size_t)strlen((const char*)book_name);
        if (length == 0 || length >= BOOK_NAME_BLOCK) return ABNORMAL;

        for (c_size_t i = 0; i < length; i++) {
                unsigned char c = book_name[i];

                if (
                        !(
                                (c >= 'A' && c <= 'Z')
                                || (c >= 'a' && c <= 'z')
                                || (c >= '0' && c <= '9')
                                || c == '-'
                                || c == '_'
                        )
                ) {
                        return ABNORMAL;
                }
        }

        return NORMAL;
}
