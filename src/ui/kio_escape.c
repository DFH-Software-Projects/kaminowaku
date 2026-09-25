// Copyright 2026 Jamison A. Drapeau
// @@ Incremental CSI/SGR/X10 decoder. Partial reads never expose control bytes.
#include "kio_escape.h"
#include <string.h>

enum {
        KIO_ST_NORMAL = 0, KIO_ST_ESCAPE, KIO_ST_CSI,
        KIO_ST_X10, KIO_ST_OSC, KIO_ST_OSC_ESCAPE, KIO_ST_DISCARD_CSI
};

static KIO_TOKEN kio_token(KIO_TOKEN_TYPE type, unsigned char ch, int wheel) {
        KIO_TOKEN t;
        t.type = type;
        t.character = ch;
        t.wheel = wheel;
        return t;
}
static KIO_TOKEN kio_none(void) { return kio_token(KIO_TOKEN_NONE, 0, 0); }

void kio_decoder_reset(KIO_DECODER *decoder) {
        if (decoder) memset(decoder, 0, sizeof(*decoder));
}

void kio_decoder_timeout(KIO_DECODER *decoder) {
        // @@ Incomplete mouse escapes expire quietly instead of becoming input.
        // Keep bracketed paste active until its explicit end or command reset.
        if (!decoder) return;
        decoder->state = KIO_ST_NORMAL;
        decoder->length = 0;
}

static int kio_number(const unsigned char **cursor, unsigned int *out) {
        unsigned int value = 0;
        unsigned int digits = 0;
        while (**cursor >= '0' && **cursor <= '9') {
                if (digits++ > 5 || value > 6553) return 0;
                value = value * 10 + (unsigned int)(*(*cursor)++ - '0');
                if (value > 65535) return 0;
        }
        if (!digits) return 0;
        *out = value;
        return 1;
}

static int kio_sgr_wheel(const unsigned char *data, unsigned int length, int *wheel) {
        unsigned int button, x, y;
        const unsigned char *p = data;
        if (!wheel || length < 8 || *p++ != '<') return 0;
        if (!kio_number(&p, &button) || *p++ != ';') return 0;
        if (!kio_number(&p, &x) || *p++ != ';') return 0;
        if (!kio_number(&p, &y)) return 0;
        if ((size_t)(p - data) + 1 != length || *p != 'M') return 0;
        (void)x; (void)y;
        // @@ Modifiers occupy bits 2-4; motion/release events aren't wheel events.
        if ((button & 0xC3U) == 64U) { *wheel = 1; return 1; }
        if ((button & 0xC3U) == 65U) { *wheel = -1; return 1; }
        return 0;
}

KIO_TOKEN kio_decoder_feed(KIO_DECODER *decoder, unsigned char byte) {
        KIO_TOKEN result = kio_none();
        unsigned int n;
        if (!decoder) return result;
        switch (decoder->state) {
                case KIO_ST_NORMAL:
                        if (byte == 0x1b) {
                                decoder->state = KIO_ST_ESCAPE;
                                decoder->length = 0;
                                return result;
                        }
                        if (decoder->paste && (byte == '\r' || byte == '\n'))
                                byte = ' ';
                        if (decoder->paste && byte < 0x20 && byte != '\t')
                                return result;
                        return kio_token(KIO_TOKEN_CHAR, byte, 0);
                case KIO_ST_ESCAPE:
                        if (byte == '[' || byte == 'O') {
                                decoder->state = KIO_ST_CSI;
                                decoder->length = 0;
                                return result;
                        }
                        if (byte == ']') {
                                decoder->state = KIO_ST_OSC;
                                return result;
                        }
                        decoder->state = KIO_ST_NORMAL;
                        return result;
                case KIO_ST_OSC:
                        if (byte == '\a') decoder->state = KIO_ST_NORMAL;
                        else if (byte == 0x1b) decoder->state = KIO_ST_OSC_ESCAPE;
                        return result;
                case KIO_ST_OSC_ESCAPE:
                        decoder->state = byte == '\\' ? KIO_ST_NORMAL : KIO_ST_OSC;
                        return result;
                case KIO_ST_X10:
                        if (decoder->length < 3)
                                decoder->buffer[decoder->length++] = byte;
                        if (decoder->length == 3) {
                                int b = (int)decoder->buffer[0] - 32;
                                decoder->state = KIO_ST_NORMAL;
                                decoder->length = 0;
                                if ((b & 0xC3) == 64)
                                        return kio_token(KIO_TOKEN_WHEEL, 0, 1);
                                if ((b & 0xC3) == 65)
                                        return kio_token(KIO_TOKEN_WHEEL, 0, -1);
                        }
                        return result;
                case KIO_ST_DISCARD_CSI:
                        if (byte >= 0x40 && byte <= 0x7e)
                                decoder->state = KIO_ST_NORMAL;
                        return result;
                case KIO_ST_CSI:
                        if (decoder->length == 0 && byte == 'M' && !decoder->paste) {
                                decoder->state = KIO_ST_X10;
                                return result;
                        }
                        if (decoder->length >= sizeof(decoder->buffer) - 1) {
                                decoder->state = KIO_ST_DISCARD_CSI;
                                decoder->length = 0;
                                return result;
                        }
                        decoder->buffer[decoder->length++] = byte;
                        if (byte < 0x40 || byte > 0x7e) return result;
                        decoder->state = KIO_ST_NORMAL;
                        n = decoder->length;
                        decoder->buffer[n] = '\0';
                        if (decoder->buffer[0] == '<') {
                                int wheel;
                                if (kio_sgr_wheel(decoder->buffer, n, &wheel))
                                        return kio_token(KIO_TOKEN_WHEEL, 0, wheel);
                                return result;
                        }
                        if (n == 1) {
                                switch (byte) {
                                        case 'A': result.type = KIO_TOKEN_UP; break;
                                        case 'B': result.type = KIO_TOKEN_DOWN; break;
                                        case 'C': result.type = KIO_TOKEN_RIGHT; break;
                                        case 'D': result.type = KIO_TOKEN_LEFT; break;
                                        case 'H': result.type = KIO_TOKEN_HOME; break;
                                        case 'F': result.type = KIO_TOKEN_END; break;
                                }
                                return result;
                        }
                        if (byte == '~') {
                                if (n == 2) {
                                        switch (decoder->buffer[0]) {
                                                case '1': case '7': result.type = KIO_TOKEN_HOME; break;
                                                case '4': case '8': result.type = KIO_TOKEN_END; break;
                                                case '3': result.type = KIO_TOKEN_DELETE; break;
                                        }
                                } else if (n == 4 &&
                                        memcmp(decoder->buffer, "200~", 4) == 0) {
                                        decoder->paste = 1;
                                        result.type = KIO_TOKEN_PASTE_START;
                                } else if (n == 4 &&
                                        memcmp(decoder->buffer, "201~", 4) == 0) {
                                        decoder->paste = 0;
                                        result.type = KIO_TOKEN_PASTE_END;
                                }
                        }
                        return result;
                default:
                        kio_decoder_timeout(decoder);
                        return result;
        }
}
