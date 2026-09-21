// Copyright 2026 Jamison A. Drapeau
#define _POSIX_C_SOURCE 200809L
#include "helpers.h"
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
// Better strcat
void strcats(char * dst, size_t dsize, const char * src, ...) {
        if (!dst || !src || dsize == 0)
                return;

        size_t used = strnlen(dst, dsize);
        if (used >= dsize)
                return;

        size_t remaining = dsize - used;

        va_list ap;
        va_start(ap, src);
        (void)vsnprintf(dst + used, remaining, src, ap);   // NULL-safe truncation
        va_end(ap);
}
// TID validation helper
int vtid(const char *TID) {
        if (!TID) return ABNORMAL;
        for (int i = 0; i < TID_BLOCK - 1; i++) {
                unsigned char c = (unsigned char)TID[i];
                if (c == '\0') return ABNORMAL; // too short
                if (!((c >= 'A' && c <= 'Z') ||
                      (c >= 'a' && c <= 'z') ||
                      (c >= '0' && c <= '9')))
                        return ABNORMAL; // invalid char
        }
        if (TID[TID_BLOCK - 1] != '\0')
                return ABNORMAL;
        return NORMAL; // valid
}
// @@ Timestamping function for logging
char * timestamp(void) {
        time_t now = time(NULL);
        if (now == ((time_t) - 1)) {
                return "[ERROR] Failed to get time.\n";
        } else {
                char * human_readable = ctime(&now);
                human_readable[strcspn(human_readable, "\n")] = '\0';
                return human_readable;
        }
}

// @@ Timestamping function for files (AI Generated, human modified)
char * file_timestamp_ns(void) {
        static char human_readable[64];
        struct timespec ts;
        struct tm tm_info;
        memset(human_readable, 0x00, sizeof(human_readable));
        if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
                snprintf(human_readable, sizeof(human_readable), "0000-00-00-00:00:00.000000000");
                return human_readable;
        }
        if (localtime_r(&ts.tv_sec, &tm_info) == NULL) {
                snprintf(human_readable, sizeof(human_readable), "0000-00-00-00:00:00.000000000");
                return human_readable;
        }
        snprintf(
                human_readable,
                sizeof(human_readable),
                "%04d-%02d-%02d-%02d:%02d:%02d.%09ld-",
                tm_info.tm_year + 1900,
                tm_info.tm_mon + 1,
                tm_info.tm_mday,
                tm_info.tm_hour,
                tm_info.tm_min,
                tm_info.tm_sec,
                ts.tv_nsec
        );
        return human_readable;
}

// @@ Check for a regular file without following symbolic links
int validate_regular_file_presence(const char * PATH) {
        struct stat INFO;

        if (!PATH || PATH[0] == 0x00) {
                return ABNORMAL;
        }

        memset(&INFO, 0x00, sizeof(INFO));

        if (lstat(PATH, &INFO) != NORMAL) {
                return ABNORMAL;
        }

        return S_ISREG(INFO.st_mode) ? NORMAL : ABNORMAL;
}

// @@ Copy a regular file without following links or replacing an existing file
int copy_regular_file(const char * SRC, const char * DST) {
        FILE * SRC_FILE;
        FILE * DST_FILE;
        struct stat INFO;
        unsigned char BUFFER[MAX_BLOCK];
        size_t READ_COUNT;
        int SRC_FD;
        int DST_FD;
        int RESULT;

        if (!SRC || !DST || SRC[0] == 0x00 || DST[0] == 0x00) {
                return ABNORMAL;
        }

        SRC_FD = open(
                SRC,
                O_RDONLY
#ifdef O_NOFOLLOW
                | O_NOFOLLOW
#endif
        );

        if (SRC_FD < 0) {
                return ABNORMAL;
        }

        memset(&INFO, 0x00, sizeof(INFO));

        if (fstat(SRC_FD, &INFO) != NORMAL || !S_ISREG(INFO.st_mode)) {
                close(SRC_FD);
                return ABNORMAL;
        }

        DST_FD = open(DST, O_WRONLY | O_CREAT | O_EXCL, 0600);

        if (DST_FD < 0) {
                close(SRC_FD);
                return ABNORMAL;
        }

        SRC_FILE = fdopen(SRC_FD, "rb");

        if (!SRC_FILE) {
                close(SRC_FD);
                close(DST_FD);
                remove(DST);
                return ABNORMAL;
        }

        DST_FILE = fdopen(DST_FD, "wb");

        if (!DST_FILE) {
                fclose(SRC_FILE);
                close(DST_FD);
                remove(DST);
                return ABNORMAL;
        }

        RESULT = NORMAL;
        memset(BUFFER, 0x00, sizeof(BUFFER));

        while ((READ_COUNT = fread(BUFFER, 1, sizeof(BUFFER), SRC_FILE)) > 0) {
                if (fwrite(BUFFER, 1, READ_COUNT, DST_FILE) != READ_COUNT) {
                        RESULT = ABNORMAL;
                        break;
                }

                memset(BUFFER, 0x00, sizeof(BUFFER));
        }

        if (ferror(SRC_FILE)) {
                RESULT = ABNORMAL;
        }

        if (fflush(DST_FILE) != NORMAL) {
                RESULT = ABNORMAL;
        }

        if (RESULT == NORMAL && fsync(fileno(DST_FILE)) != NORMAL) {
                RESULT = ABNORMAL;
        }

        if (fclose(DST_FILE) != NORMAL) {
                RESULT = ABNORMAL;
        }

        fclose(SRC_FILE);

        if (RESULT == ABNORMAL) {
                remove(DST);
                return ABNORMAL;
        }

        return validate_regular_file_presence(DST);
}

