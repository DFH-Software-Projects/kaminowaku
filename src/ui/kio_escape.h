// Copyright 2026 Jamison A. Drapeau
// @@ Persistent terminal-input decoder. Every byte is consumed exactly once.
#ifndef KIO_ESCAPE_H
#define KIO_ESCAPE_H
#include <stddef.h>

typedef enum {
        KIO_TOKEN_NONE = 0,
        KIO_TOKEN_CHAR,
        KIO_TOKEN_UP,
        KIO_TOKEN_DOWN,
        KIO_TOKEN_LEFT,
        KIO_TOKEN_RIGHT,
        KIO_TOKEN_HOME,
        KIO_TOKEN_END,
        KIO_TOKEN_DELETE,
        KIO_TOKEN_WHEEL,
        KIO_TOKEN_PASTE_START,
        KIO_TOKEN_PASTE_END
} KIO_TOKEN_TYPE;

typedef struct {
        KIO_TOKEN_TYPE type;
        unsigned char character;
        int wheel; // +1 = older output, -1 = newer output
} KIO_TOKEN;

typedef struct {
        unsigned int state;
        unsigned int length;
        unsigned char buffer[48];
        unsigned int paste;
} KIO_DECODER;

void kio_decoder_reset(KIO_DECODER *decoder);
KIO_TOKEN kio_decoder_feed(KIO_DECODER *decoder, unsigned char byte);
void kio_decoder_timeout(KIO_DECODER *decoder);
#endif
