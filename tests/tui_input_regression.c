// Copyright 2026 Jamison A. Drapeau
// @@ Regression coverage for split mouse bursts and mixed keyboard input.
#include "kio_escape.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void feed(KIO_DECODER *d, const unsigned char *s, size_t n,
        int *wheel, char *typed, size_t *used) {
        for (size_t i = 0; i < n; i++) {
                KIO_TOKEN t = kio_decoder_feed(d, s[i]);
                if (t.type == KIO_TOKEN_WHEEL) *wheel += t.wheel;
                if (t.type == KIO_TOKEN_CHAR) typed[(*used)++] = (char)t.character;
        }
}
int main(void) {
        KIO_DECODER d;
        int wheel = 0;
        char typed[256] = {0};
        size_t used = 0;
        kio_decoder_reset(&d);

        // @@ An SGR event split at every possible byte boundary never leaks
        // printable fragments or consumes the character immediately after it.
        const char *sgr = "\033[<64;61;71M";
        for (size_t split = 0; split <= strlen(sgr); split++) {
                kio_decoder_reset(&d); wheel = 0; used = 0;
                feed(&d, (const unsigned char *)sgr, split, &wheel, typed, &used);
                feed(&d, (const unsigned char *)sgr + split, strlen(sgr) - split,
                        &wheel, typed, &used);
                feed(&d, (const unsigned char *)"Z", 1, &wheel, typed, &used);
                assert(wheel == 1 && used == 1 && typed[0] == 'Z');
        }

        // @@ The full stream can contain multiple mouse events, arrow keys and
        // ordinary command input in exactly one read.
        kio_decoder_reset(&d); wheel = 0; used = 0;
        const unsigned char burst[] = "\033[<64;1;1M\033[<65;1;1Mabc\033[A";
        feed(&d, burst, sizeof(burst) - 1, &wheel, typed, &used);
        assert(wheel == 0 && used == 3 && memcmp(typed, "abc", 3) == 0);
        assert(kio_decoder_feed(&d, 'x').type == KIO_TOKEN_CHAR);

        // @@ Partial X10 mouse events consume exactly three payload bytes.
        kio_decoder_reset(&d); wheel = 0; used = 0;
        const unsigned char x10[] = {27, '[', 'M', 96, 42, 43, 'Q'};
        feed(&d, x10, sizeof(x10), &wheel, typed, &used);
        assert(wheel == 1 && used == 1 && typed[0] == 'Q');

        // @@ Bracketed paste never turns pasted newlines into command submits.
        kio_decoder_reset(&d); wheel = 0; used = 0;
        const char *paste = "\033[200~scan -p 80\nhelp\033[201~X";
        feed(&d, (const unsigned char *)paste, strlen(paste), &wheel, typed, &used);
        assert(used == strlen("scan -p 80 helpX"));
        assert(memcmp(typed, "scan -p 80 helpX", used) == 0);
        assert(d.paste == 0);

        // @@ A malformed, overlong CSI is discarded, not printed or parsed.
        kio_decoder_reset(&d); wheel = 0; used = 0;
        feed(&d, (const unsigned char *)"\033[<1234567890123456789012345678901234567890123456789012345M",
                strlen("\033[<1234567890123456789012345678901234567890123456789012345M"),
                &wheel, typed, &used);
        assert(wheel == 0);

        // @@ A timeout must not leak partial escapes into the prompt.
        kio_decoder_reset(&d); wheel = 0; used = 0;
        feed(&d, (const unsigned char *)"\033[<64;", 6, &wheel, typed, &used);
        kio_decoder_timeout(&d);
        assert(wheel == 0 && used == 0);
        puts("PASS: split SGR, X10, mixed keys, paste, malformed and timeout");
        return 0;
}
