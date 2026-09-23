// Copyright 2026 Jamison A. Drapeau
#include "book_tls.h"
#include "kwire_deadline.h"
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

struct BOOK_TLS {
        BOOK_SESSION * session;
        SSL_CTX *      context;
        SSL *          ssl;
        BIO_METHOD *   method;
        int32_t        timeout_ms;
        KWIRE_DEADLINE deadline;
        int8_t         deadline_active;
        nosix_status_t last_status;
};

static int book_tls_begin_deadline(BOOK_TLS * tls) {
        if (!tls || kwire_deadline_start(&tls->deadline, tls->timeout_ms) != NORMAL) {
                return ABNORMAL;
        }
        tls->deadline_active = ISTRUE;
        return NORMAL;
}

static int32_t book_tls_remaining_ms(BOOK_TLS * tls) {
        if (!tls) return -1;
        return tls->deadline_active == ISTRUE
                ? kwire_deadline_remaining_ms(&tls->deadline)
                : tls->timeout_ms;
}

static int book_tls_bio_create(BIO * bio) {
        BIO_set_init(bio, 1);
        BIO_set_data(bio, NULL);
        return 1;
}

static int book_tls_bio_destroy(BIO * bio) {
        if (!bio) return 0;
        BIO_set_init(bio, 0);
        BIO_set_data(bio, NULL);
        return 1;
}

static long book_tls_bio_ctrl(BIO * bio, int cmd, long num, void * ptr) {
        (void)bio;
        (void)num;
        (void)ptr;
        if (cmd == BIO_CTRL_FLUSH) return 1;
        return 0;
}

static int book_tls_bio_write(BIO * bio, const char * data, int length) {
        BOOK_TLS * tls;
        size_t written = 0;
        nosix_status_t status;
        int32_t remaining_ms;

        if (!bio || !data || length <= 0) return 0;
        tls = (BOOK_TLS*)BIO_get_data(bio);
        if (!tls || !tls->session) return -1;

        BIO_clear_retry_flags(bio);
        remaining_ms = book_tls_remaining_ms(tls);
        if (remaining_ms <= 0) {
                tls->last_status = remaining_ms == 0 ? NOSIX_TIMEOUT : NOSIX_ERR_SYSTEM;
                if (remaining_ms == 0) BIO_set_retry_write(bio);
                return -1;
        }
        status = books_session_stream_write(
                tls->session,
                (const uint8_t*)data,
                (size_t)length,
                &written,
                remaining_ms
        );
        tls->last_status = status;

        if (status == NOSIX_OK && written > 0) return (int)written;
        if (status == NOSIX_TIMEOUT) BIO_set_retry_write(bio);
        return -1;
}

static int book_tls_bio_read(BIO * bio, char * data, int length) {
        BOOK_TLS * tls;
        size_t received = 0;
        nosix_status_t status;
        int32_t remaining_ms;

        if (!bio || !data || length <= 0) return 0;
        tls = (BOOK_TLS*)BIO_get_data(bio);
        if (!tls || !tls->session) return -1;

        BIO_clear_retry_flags(bio);
        remaining_ms = book_tls_remaining_ms(tls);
        if (remaining_ms <= 0) {
                tls->last_status = remaining_ms == 0 ? NOSIX_TIMEOUT : NOSIX_ERR_SYSTEM;
                if (remaining_ms == 0) BIO_set_retry_read(bio);
                return -1;
        }
        status = books_session_stream_read(
                tls->session,
                (uint8_t*)data,
                (size_t)length,
                &received,
                remaining_ms
        );
        tls->last_status = status;

        if (received > 0) return (int)received;
        if (status == NOSIX_EOF) return 0;
        if (status == NOSIX_TIMEOUT) BIO_set_retry_read(bio);
        return -1;
}

static book_tls_status_t book_tls_map(BOOK_TLS * tls, int ssl_error) {
        if (ssl_error == SSL_ERROR_ZERO_RETURN) return BOOK_TLS_EOF;
        if (ssl_error == SSL_ERROR_WANT_READ || ssl_error == SSL_ERROR_WANT_WRITE) {
                return BOOK_TLS_TIMEOUT;
        }

        if (tls) {
                if (tls->last_status == NOSIX_TIMEOUT) return BOOK_TLS_TIMEOUT;
                if (tls->last_status == NOSIX_EOF) return BOOK_TLS_EOF;
                if (tls->last_status == NOSIX_ERR_CONNECTION) return BOOK_TLS_RESET;
        }
        return BOOK_TLS_ERROR;
}

int book_tls_open(
        BOOK_SESSION * session,
        BOOK_TLS ** tls,
        const char * server_name,
        int32_t timeout_ms
) {
        BOOK_TLS * created;
        BIO * bio;
        int rc;

        if (!session || !tls || *tls || timeout_ms <= 0) return ABNORMAL;
        if (
                books_session_network_allowed(session) != ISTRUE
                || !session->stream
                || session->transport != BOOK_TRANSPORT_TCP
        ) return ABNORMAL;

        created = calloc(1, sizeof(*created));
        if (!created) return ABNORMAL;

        created->session = session;
        created->timeout_ms = timeout_ms;
        created->last_status = NOSIX_OK;

        created->context = SSL_CTX_new(TLS_client_method());
        if (!created->context) goto fail;
        SSL_CTX_set_verify(created->context, SSL_VERIFY_NONE, NULL);
        (void)SSL_CTX_set_min_proto_version(created->context, TLS1_VERSION);

        created->ssl = SSL_new(created->context);
        if (!created->ssl) goto fail;

        created->method = BIO_meth_new(BIO_TYPE_SOURCE_SINK, "kaminowaku-nosix");
        if (!created->method) goto fail;

        if (
                BIO_meth_set_create(created->method, book_tls_bio_create) != 1
                || BIO_meth_set_destroy(created->method, book_tls_bio_destroy) != 1
                || BIO_meth_set_ctrl(created->method, book_tls_bio_ctrl) != 1
                || BIO_meth_set_write(created->method, book_tls_bio_write) != 1
                || BIO_meth_set_read(created->method, book_tls_bio_read) != 1
        ) goto fail;

        bio = BIO_new(created->method);
        if (!bio) goto fail;
        BIO_set_data(bio, created);
        BIO_set_init(bio, 1);

        SSL_set_bio(created->ssl, bio, bio);
        SSL_set_connect_state(created->ssl);

        if (server_name && server_name[0] != 0x00) {
                if (SSL_set_tlsext_host_name(created->ssl, server_name) != 1) goto fail;
        }

        if (book_tls_begin_deadline(created) != NORMAL) goto fail;
        rc = SSL_connect(created->ssl);
        if (rc != 1) goto fail;

        *tls = created;
        return NORMAL;

fail:
        if (created->ssl) SSL_free(created->ssl);
        if (created->method) BIO_meth_free(created->method);
        if (created->context) SSL_CTX_free(created->context);
        memset(created, 0x00, sizeof(*created));
        free(created);
        return ABNORMAL;
}

int book_tls_set_timeout(
        BOOK_TLS * tls,
        int32_t timeout_ms
) {
        if (!tls || timeout_ms <= 0) return ABNORMAL;
        tls->timeout_ms = timeout_ms;
        return NORMAL;
}

book_tls_status_t book_tls_write(
        BOOK_TLS * tls,
        const unsigned char * data,
        c_size_t length,
        c_size_t * written
) {
        int rc;

        if (written) *written = 0;
        if (!tls || !tls->ssl || (!data && length > 0)) return BOOK_TLS_ERROR;
        if (length == 0) return BOOK_TLS_OK;
        if (length > (c_size_t)INT_MAX) return BOOK_TLS_ERROR;

        if (book_tls_begin_deadline(tls) != NORMAL) return BOOK_TLS_ERROR;
        rc = SSL_write(tls->ssl, data, (int)length);
        if (rc > 0) {
                if (written) *written = (c_size_t)rc;
                return BOOK_TLS_OK;
        }
        return book_tls_map(tls, SSL_get_error(tls->ssl, rc));
}

book_tls_status_t book_tls_read(
        BOOK_TLS * tls,
        unsigned char * data,
        c_size_t capacity,
        c_size_t * received
) {
        int rc;

        if (received) *received = 0;
        if (!tls || !tls->ssl || !data || capacity == 0 || capacity > (c_size_t)INT_MAX) {
                return BOOK_TLS_ERROR;
        }

        if (book_tls_begin_deadline(tls) != NORMAL) return BOOK_TLS_ERROR;
        rc = SSL_read(tls->ssl, data, (int)capacity);
        if (rc > 0) {
                if (received) *received = (c_size_t)rc;
                return BOOK_TLS_OK;
        }
        return book_tls_map(tls, SSL_get_error(tls->ssl, rc));
}

void book_tls_close(BOOK_TLS ** tls) {
        BOOK_TLS * state;

        if (!tls || !*tls) return;
        state = *tls;

        if (state->ssl) {
                // Cleanup must not inherit an entire read timeout.
                (void)kwire_deadline_start(&state->deadline, 1);
                state->deadline_active = ISTRUE;
                (void)SSL_shutdown(state->ssl);
                SSL_free(state->ssl);
                state->ssl = NULL;
        }
        if (state->method) {
                BIO_meth_free(state->method);
                state->method = NULL;
        }
        if (state->context) {
                SSL_CTX_free(state->context);
                state->context = NULL;
        }

        memset(state, 0x00, sizeof(*state));
        free(state);
        *tls = NULL;
}
