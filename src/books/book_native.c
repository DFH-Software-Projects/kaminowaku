// Copyright 2026 Jamison A. Drapeau
#include "book_native.h"
#include "book_audit.h"
#include "book_output.h"
#include "book_tls.h"
#include "kui.h"
#include "kwire_deadline.h"
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct BOOK_NATIVE_STATE {
        BOOK_TLS * tls;
        BOOK_VALUE private_table;
} BOOK_NATIVE_STATE;

static BOOK_NATIVE_STATE * book_native_state(BOOK_RUNTIME * runtime) {
        return runtime
                ? (BOOK_NATIVE_STATE*)book_runtime_native_state(runtime)
                : NULL;
}

static const char * book_native_transport_name(
        const BOOK_SESSION * session
) {
        if (!session) return "none";

        switch (session->transport) {
                case BOOK_TRANSPORT_TCP: return "tcp";
                case BOOK_TRANSPORT_UDP: return "udp";
                case BOOK_TRANSPORT_NONE:
                default:                 return "none";
        }
}

static const char * book_native_family_name(
        const BOOK_SESSION * session
) {
        if (!session) return "none";

        switch (session->address_family) {
                case NOSIX_ADDRESS_IPV4: return "ipv4";
                case NOSIX_ADDRESS_IPV6: return "ipv6";
                case NOSIX_ADDRESS_NONE:
                default:                 return "none";
        }
}

static const char * book_native_status_name(nosix_status_t status) {
        switch (status) {
                case NOSIX_OK:                  return "ok";
                case NOSIX_TIMEOUT:             return "timeout";
                case NOSIX_TRUNCATED:           return "limit";
                case NOSIX_EOF:                 return "eof";
                case NOSIX_ERR_CONNECTION:      return "reset";
                case NOSIX_ERR_ARGUMENT:        return "argument";
                case NOSIX_ERR_STATE:           return "state";
                case NOSIX_ERR_MEMORY:          return "memory";
                case NOSIX_ERR_ADDRESS:         return "address";
                case NOSIX_ERR_ROUTE:           return "route";
                case NOSIX_ERR_NEIGHBOR:        return "neighbor";
                case NOSIX_ERR_UNSUPPORTED:     return "unsupported";
                case NOSIX_ERR_FRAME_TOO_LARGE: return "limit";
                case NOSIX_ERR_SYSTEM:
                default:                        return "error";
        }
}

static const char * book_native_tls_status_name(book_tls_status_t status) {
        switch (status) {
                case BOOK_TLS_OK:      return "ok";
                case BOOK_TLS_TIMEOUT: return "timeout";
                case BOOK_TLS_EOF:     return "eof";
                case BOOK_TLS_RESET:   return "reset";
                case BOOK_TLS_ERROR:
                default:               return "error";
        }
}

static int book_native_return1(
        BOOK_MULTI_VALUE * returns,
        BOOK_VALUE first
) {
        if (!returns) return ABNORMAL;

        memset(returns, 0x00, sizeof(*returns));
        returns->values[0] = first;
        returns->count = 1;
        return NORMAL;
}

static int book_native_return2(
        BOOK_MULTI_VALUE * returns,
        BOOK_VALUE first,
        BOOK_VALUE second
) {
        if (!returns) return ABNORMAL;

        memset(returns, 0x00, sizeof(*returns));
        returns->values[0] = first;
        returns->values[1] = second;
        returns->count = 2;
        return NORMAL;
}

static int book_native_string(
        BOOK_RUNTIME * runtime,
        const unsigned char * bytes,
        c_size_t length,
        BOOK_VALUE * value
) {
        if (!runtime || !value || (!bytes && length > 0)) return ABNORMAL;

        return book_runtime_make_string(
                runtime,
                bytes,
                length,
                value
        );
}

static int book_native_cstring(
        BOOK_RUNTIME * runtime,
        const char * text,
        BOOK_VALUE * value
) {
        if (!text) text = "";

        return book_native_string(
                runtime,
                (const unsigned char*)text,
                (c_size_t)strlen(text),
                value
        );
}

static int book_native_status_value(
        BOOK_RUNTIME * runtime,
        const char * status,
        BOOK_VALUE * value
) {
        return book_native_cstring(runtime, status, value);
}

static int book_native_integer(
        BOOK_RUNTIME * runtime,
        const BOOK_VALUE * arguments,
        c_size_t argument_count,
        c_size_t index,
        int64_t minimum,
        int64_t maximum,
        int64_t * result,
        const char * message
) {
        double number;
        int64_t integer;

        if (
                !runtime
                || !arguments
                || !result
                || index >= argument_count
                || book_value_number(
                        &arguments[index],
                        &number
                ) != NORMAL
                || number < -9223372036854775808.0
                || number >= 9223372036854775808.0
        ) {
                return book_runtime_native_fail(runtime, message);
        }

        integer = (int64_t)number;
        if (
                (double)integer != number
                || integer < minimum
                || integer > maximum
        ) {
                return book_runtime_native_fail(runtime, message);
        }

        *result = integer;
        return NORMAL;
}

static int32_t book_native_default_timeout(BOOK_RUNTIME * runtime) {
        BOOK_SESSION * session = book_runtime_session(runtime);
        int32_t limit = KWIRE_RX_DEFAULT_TIMEOUT_MS;
        if (
                session && session->prog_data
                && session->prog_data->gprof.rx_timeout_ms > 0
        ) {
                limit = session->prog_data->gprof.rx_timeout_ms;
        }
        return limit > KWIRE_RX_HARD_TIMEOUT_MS
                ? KWIRE_RX_HARD_TIMEOUT_MS : limit;
}

static int book_native_timeout(
        BOOK_RUNTIME * runtime,
        const BOOK_VALUE * arguments,
        c_size_t argument_count,
        c_size_t index,
        int32_t * timeout_ms
) {
        int64_t timeout;
        int32_t maximum;
        if (!runtime || !timeout_ms) return ABNORMAL;
        maximum = book_native_default_timeout(runtime);

        if (index >= argument_count || arguments[index].type == BOOK_VALUE_NIL) {
                *timeout_ms = maximum;
                return NORMAL;
        }

        if (
                book_native_integer(
                        runtime,
                        arguments,
                        argument_count,
                        index,
                        1,
                        INT32_MAX,
                        &timeout,
                        "timeout_ms must be a positive integer"
                ) != NORMAL
        ) return ABNORMAL;

        if (timeout > maximum) {
                return book_runtime_native_fail(
                        runtime,
                        "timeout_ms exceeds the active profile/native hard maximum"
                );
        }

        *timeout_ms = (int32_t)timeout;
        return NORMAL;
}

static int book_native_table_set_string_value(
        BOOK_RUNTIME * runtime,
        BOOK_VALUE table,
        const char * key,
        const char * text
) {
        BOOK_VALUE value;

        if (
                book_native_cstring(
                        runtime,
                        text,
                        &value
                ) != NORMAL
        ) {
                return ABNORMAL;
        }

        return book_runtime_table_set_string(
                runtime,
                table,
                key,
                value
        );
}

static int book_native_table_set_number(
        BOOK_RUNTIME * runtime,
        BOOK_VALUE table,
        const char * key,
        double number
) {
        return book_runtime_table_set_string(
                runtime,
                table,
                key,
                book_runtime_number(number)
        );
}

static int book_native_table_set_boolean(
        BOOK_RUNTIME * runtime,
        BOOK_VALUE table,
        const char * key,
        int8_t boolean
) {
        return book_runtime_table_set_string(
                runtime,
                table,
                key,
                book_runtime_boolean(boolean)
        );
}

static const char * book_native_target_host(
        const BOOK_SESSION * session
) {
        if (
                !session
                || !session->target
                || !session->target->PETAL
        ) {
                return "";
        }

        if (session->target->PETAL->URL[0] != 0x00) {
                return (const char*)session->target->PETAL->URL;
        }
        if (session->target->PETAL->IPV4[0] != 0x00) {
                return (const char*)session->target->PETAL->IPV4;
        }
        if (session->target->PETAL->IPV6[0] != 0x00) {
                return (const char*)session->target->PETAL->IPV6;
        }

        return "";
}

static int book_native_target_table(
        BOOK_RUNTIME * runtime,
        BOOK_VALUE * target
) {
        BOOK_SESSION * session;
        const char * ipv4 = "";
        const char * ipv6 = "";
        const char * tid = "";

        if (!runtime || !target) return ABNORMAL;
        session = book_runtime_session(runtime);

        if (
                session
                && session->target
                && session->target->PETAL
        ) {
                ipv4 = (const char*)session->target->PETAL->IPV4;
                ipv6 = (const char*)session->target->PETAL->IPV6;
                tid = (const char*)session->target->TID;
        }

        if (
                book_runtime_make_table(runtime, target) != NORMAL
                || book_native_table_set_string_value(
                        runtime,
                        *target,
                        "host",
                        book_native_target_host(session)
                ) != NORMAL
                || book_native_table_set_string_value(
                        runtime,
                        *target,
                        "ipv4",
                        ipv4
                ) != NORMAL
                || book_native_table_set_string_value(
                        runtime,
                        *target,
                        "ipv6",
                        ipv6
                ) != NORMAL
                || book_native_table_set_string_value(
                        runtime,
                        *target,
                        "tid",
                        tid
                ) != NORMAL
        ) {
                return ABNORMAL;
        }

        return NORMAL;
}

static int book_native_session_info(
        BOOK_RUNTIME * runtime,
        const BOOK_VALUE * arguments,
        c_size_t argument_count,
        BOOK_MULTI_VALUE * returns
) {
        BOOK_SESSION * session;
        BOOK_NATIVE_STATE * state;
        BOOK_VALUE info;

        (void)arguments;

        if (!runtime || !returns) return ABNORMAL;
        if (argument_count != 0) {
                return book_runtime_native_fail(
                        runtime,
                        "_kami.session_info() takes no arguments"
                );
        }

        session = book_runtime_session(runtime);
        state = book_native_state(runtime);

        if (
                !session
                || book_runtime_make_table(runtime, &info) != NORMAL
                || book_native_table_set_string_value(
                        runtime,
                        info,
                        "book",
                        (const char*)session->book_name
                ) != NORMAL
                || book_native_table_set_string_value(
                        runtime,
                        info,
                        "tid",
                        (const char*)session->tid
                ) != NORMAL
                || book_native_table_set_string_value(
                        runtime,
                        info,
                        "target",
                        book_native_target_host(session)
                ) != NORMAL
                || book_native_table_set_string_value(
                        runtime,
                        info,
                        "transport",
                        book_native_transport_name(session)
                ) != NORMAL
                || book_native_table_set_string_value(
                        runtime,
                        info,
                        "family",
                        book_native_family_name(session)
                ) != NORMAL
                || book_native_table_set_number(
                        runtime,
                        info,
                        "remote_port",
                        (double)session->destination_port
                ) != NORMAL
                || book_native_table_set_number(
                        runtime,
                        info,
                        "local_port",
                        (double)session->local_port
                ) != NORMAL
                || book_native_table_set_number(
                        runtime,
                        info,
                        "network_actions",
                        (double)session->network_actions
                ) != NORMAL
                || book_native_table_set_number(
                        runtime,
                        info,
                        "tx_bytes",
                        (double)session->tx_bytes
                ) != NORMAL
                || book_native_table_set_number(
                        runtime,
                        info,
                        "rx_bytes",
                        (double)session->rx_bytes
                ) != NORMAL
                || book_native_table_set_boolean(
                        runtime,
                        info,
                        "tls",
                        state && state->tls ? ISTRUE : ISFALSE
                ) != NORMAL
        ) {
                return ABNORMAL;
        }

        return book_native_return1(returns, info);
}

static int book_native_open_transport(
        BOOK_RUNTIME * runtime,
        const BOOK_VALUE * arguments,
        c_size_t argument_count,
        BOOK_MULTI_VALUE * returns,
        book_transport_t transport
) {
        BOOK_SESSION * session;
        int64_t port;
        int32_t timeout_ms;
        nosix_status_t status;
        BOOK_VALUE status_value;

        if (!runtime || !returns) return ABNORMAL;
        session = book_runtime_session(runtime);

        if (
                !session
                || book_native_integer(
                        runtime,
                        arguments,
                        argument_count,
                        0,
                        1,
                        65535,
                        &port,
                        "transport port must be an integer between 1 and 65535"
                ) != NORMAL
                || book_native_timeout(
                        runtime,
                        arguments,
                        argument_count,
                        1,
                        &timeout_ms
                ) != NORMAL
        ) {
                return ABNORMAL;
        }

        status = transport == BOOK_TRANSPORT_TCP
                ? books_session_open_tcp(
                        session,
                        (uint16_t)port,
                        timeout_ms
                )
                : books_session_open_udp(
                        session,
                        (uint16_t)port,
                        timeout_ms
                );

        if (
                book_native_status_value(
                        runtime,
                        book_native_status_name(status),
                        &status_value
                ) != NORMAL
        ) {
                return ABNORMAL;
        }

        return book_native_return2(
                returns,
                book_runtime_boolean(
                        status == NOSIX_OK ? ISTRUE : ISFALSE
                ),
                status_value
        );
}

static int book_native_tcp_open(
        BOOK_RUNTIME * runtime,
        const BOOK_VALUE * arguments,
        c_size_t argument_count,
        BOOK_MULTI_VALUE * returns
) {
        return book_native_open_transport(
                runtime,
                arguments,
                argument_count,
                returns,
                BOOK_TRANSPORT_TCP
        );
}

static int book_native_udp_open(
        BOOK_RUNTIME * runtime,
        const BOOK_VALUE * arguments,
        c_size_t argument_count,
        BOOK_MULTI_VALUE * returns
) {
        return book_native_open_transport(
                runtime,
                arguments,
                argument_count,
                returns,
                BOOK_TRANSPORT_UDP
        );
}

static int book_native_tx(
        BOOK_RUNTIME * runtime,
        const BOOK_VALUE * arguments,
        c_size_t argument_count,
        BOOK_MULTI_VALUE * returns
) {
        BOOK_SESSION * session;
        BOOK_NATIVE_STATE * state;
        const unsigned char * data;
        c_size_t length;
        int32_t timeout_ms;
        c_size_t tls_written = 0;
        size_t written = 0;
        const char * status_text;
        BOOK_VALUE status_value;

        if (!runtime || !returns) return ABNORMAL;
        session = book_runtime_session(runtime);
        state = book_native_state(runtime);

        if (
                !session
                || argument_count == 0
                || book_value_string(
                        &arguments[0],
                        &data,
                        &length
                ) != NORMAL
        ) {
                return book_runtime_native_fail(
                        runtime,
                        "_kami.tx() requires a string payload"
                );
        }

        if (
                length > BOOK_NATIVE_IO_LIMIT
                || book_native_timeout(
                        runtime,
                        arguments,
                        argument_count,
                        1,
                        &timeout_ms
                ) != NORMAL
        ) {
                return book_runtime_native_fail(
                        runtime,
                        "transmit payload exceeds Book I/O limit"
                );
        }

        if (state && state->tls) {
                book_tls_status_t tls_status;

                if (
                        book_tls_set_timeout(
                                state->tls,
                                timeout_ms
                        ) != NORMAL
                ) {
                        return book_runtime_native_fail(
                                runtime,
                                "failed to apply TLS write timeout"
                        );
                }

                tls_status = book_tls_write(
                        state->tls,
                        data,
                        length,
                        &tls_written
                );

                status_text = book_native_tls_status_name(tls_status);
                written = (size_t)tls_written;
        } else {
                nosix_status_t status = books_session_payload_write(
                        session,
                        data,
                        (size_t)length,
                        &written,
                        timeout_ms
                );

                status_text = book_native_status_name(status);
        }

        if (
                book_native_status_value(
                        runtime,
                        status_text,
                        &status_value
                ) != NORMAL
        ) {
                return ABNORMAL;
        }

        return book_native_return2(
                returns,
                book_runtime_number((double)written),
                status_value
        );
}

static int book_native_rx_once(
        BOOK_RUNTIME * runtime,
        c_size_t capacity,
        int32_t timeout_ms,
        unsigned char * buffer,
        c_size_t * received,
        const char ** status_text
) {
        BOOK_SESSION * session;
        BOOK_NATIVE_STATE * state;

        if (
                !runtime
                || !buffer
                || !received
                || !status_text
                || capacity == 0
        ) {
                return ABNORMAL;
        }

        session = book_runtime_session(runtime);
        state = book_native_state(runtime);
        *received = 0;

        if (!session) return ABNORMAL;

        if (state && state->tls) {
                book_tls_status_t status;

                if (
                        book_tls_set_timeout(
                                state->tls,
                                timeout_ms
                        ) != NORMAL
                ) {
                        return book_runtime_native_fail(
                                runtime,
                                "failed to apply TLS read timeout"
                        );
                }

                status = book_tls_read(
                        state->tls,
                        buffer,
                        capacity,
                        received
                );

                *status_text = book_native_tls_status_name(status);
                return NORMAL;
        }

        {
                size_t raw_received = 0;
                nosix_status_t status = books_session_payload_read(
                        session,
                        buffer,
                        (size_t)capacity,
                        &raw_received,
                        timeout_ms
                );

                if (raw_received > capacity) raw_received = capacity;
                *received = (c_size_t)raw_received;
                *status_text = book_native_status_name(status);
        }

        return NORMAL;
}

static int book_native_rx(
        BOOK_RUNTIME * runtime,
        const BOOK_VALUE * arguments,
        c_size_t argument_count,
        BOOK_MULTI_VALUE * returns
) {
        int64_t capacity_number;
        c_size_t capacity;
        int32_t timeout_ms;
        unsigned char * buffer;
        c_size_t received = 0;
        const char * status_text = "error";
        BOOK_VALUE data_value;
        BOOK_VALUE status_value;

        if (!runtime || !returns) return ABNORMAL;

        if (
                book_native_integer(
                        runtime,
                        arguments,
                        argument_count,
                        0,
                        1,
                        BOOK_NATIVE_IO_LIMIT,
                        &capacity_number,
                        "receive size must be a positive bounded integer"
                ) != NORMAL
                || book_native_timeout(
                        runtime,
                        arguments,
                        argument_count,
                        1,
                        &timeout_ms
                ) != NORMAL
        ) {
                return ABNORMAL;
        }

        capacity = (c_size_t)capacity_number;
        buffer = malloc(capacity);
        if (!buffer) {
                return book_runtime_native_fail(
                        runtime,
                        "failed to allocate receive buffer"
                );
        }

        if (
                book_native_rx_once(
                        runtime,
                        capacity,
                        timeout_ms,
                        buffer,
                        &received,
                        &status_text
                ) != NORMAL
                || book_native_status_value(
                        runtime,
                        status_text,
                        &status_value
                ) != NORMAL
        ) {
                memset(buffer, 0x00, capacity);
                free(buffer);
                return ABNORMAL;
        }

        if (received > 0) {
                if (
                        book_native_string(
                                runtime,
                                buffer,
                                received,
                                &data_value
                        ) != NORMAL
                ) {
                        memset(buffer, 0x00, capacity);
                        free(buffer);
                        return ABNORMAL;
                }
        } else {
                data_value = book_runtime_nil();
        }

        memset(buffer, 0x00, capacity);
        free(buffer);
        return book_native_return2(
                returns,
                data_value,
                status_value
        );
}

static int book_native_rx_exact(
        BOOK_RUNTIME * runtime,
        const BOOK_VALUE * arguments,
        c_size_t argument_count,
        BOOK_MULTI_VALUE * returns
) {
        BOOK_SESSION * session;
        int64_t count_number;
        c_size_t count;
        int32_t timeout_ms;
        KWIRE_DEADLINE DEADLINE;
        unsigned char * buffer;
        c_size_t total = 0;
        const char * final_status = "ok";
        BOOK_VALUE data_value;
        BOOK_VALUE status_value;

        if (!runtime || !returns) return ABNORMAL;
        session = book_runtime_session(runtime);

        if (
                !session
                || session->transport != BOOK_TRANSPORT_TCP
        ) {
                return book_runtime_native_fail(
                        runtime,
                        "_kami.rx_exact() requires a TCP transport"
                );
        }

        if (
                book_native_integer(
                        runtime,
                        arguments,
                        argument_count,
                        0,
                        1,
                        BOOK_NATIVE_IO_LIMIT,
                        &count_number,
                        "exact receive size must be a positive bounded integer"
                ) != NORMAL
                || book_native_timeout(
                        runtime,
                        arguments,
                        argument_count,
                        1,
                        &timeout_ms
                ) != NORMAL
        ) {
                return ABNORMAL;
        }

        count = (c_size_t)count_number;
        buffer = malloc(count);
        if (!buffer) {
                return book_runtime_native_fail(
                        runtime,
                        "failed to allocate exact receive buffer"
                );
        }

        if (kwire_deadline_start(&DEADLINE, timeout_ms) != NORMAL) {
                memset(buffer, 0x00, count);
                free(buffer);
                return book_runtime_native_fail(runtime, "unable to start receive deadline");
        }

        while (total < count) {
                int32_t remaining_ms = kwire_deadline_remaining_ms(&DEADLINE);
                c_size_t received = 0;
                const char * status_text = "error";
                if (remaining_ms <= 0) {
                        final_status = remaining_ms == 0 ? "timeout" : "error";
                        break;
                }

                if (
                        book_native_rx_once(
                                runtime,
                                count - total,
                                remaining_ms,
                                buffer + total,
                                &received,
                                &status_text
                        ) != NORMAL
                ) {
                        final_status = "error";
                        break;
                }

                total += received;
                final_status = status_text;

                if (strcmp(status_text, "ok") != MATCH) break;
                if (received == 0) {
                        final_status = "eof";
                        break;
                }
        }

        if (total == count) final_status = "ok";

        if (
                book_native_status_value(
                        runtime,
                        final_status,
                        &status_value
                ) != NORMAL
        ) {
                memset(buffer, 0x00, count);
                free(buffer);
                return ABNORMAL;
        }

        if (total > 0) {
                if (
                        book_native_string(
                                runtime,
                                buffer,
                                total,
                                &data_value
                        ) != NORMAL
                ) {
                        memset(buffer, 0x00, count);
                        free(buffer);
                        return ABNORMAL;
                }
        } else {
                data_value = book_runtime_nil();
        }

        memset(buffer, 0x00, count);
        free(buffer);
        return book_native_return2(
                returns,
                data_value,
                status_value
        );
}

static int book_native_tls_open_native(
        BOOK_RUNTIME * runtime,
        const BOOK_VALUE * arguments,
        c_size_t argument_count,
        BOOK_MULTI_VALUE * returns
) {
        BOOK_NATIVE_STATE * state;
        BOOK_SESSION * session;
        const unsigned char * server_name = (const unsigned char*)"";
        c_size_t server_name_length = 0;
        int32_t timeout_ms;
        char * name = NULL;
        BOOK_VALUE status_value;
        int status;

        if (!runtime || !returns) return ABNORMAL;
        state = book_native_state(runtime);
        session = book_runtime_session(runtime);

        if (!state || !session) return ABNORMAL;

        if (state->tls) {
                if (
                        book_native_status_value(
                                runtime,
                                "ok",
                                &status_value
                        ) != NORMAL
                ) {
                        return ABNORMAL;
                }

                return book_native_return2(
                        returns,
                        book_runtime_boolean(ISTRUE),
                        status_value
                );
        }

        if (
                argument_count > 0
                && arguments[0].type != BOOK_VALUE_NIL
                && book_value_string(
                        &arguments[0],
                        &server_name,
                        &server_name_length
                ) != NORMAL
        ) {
                return book_runtime_native_fail(
                        runtime,
                        "TLS server_name must be a string"
                );
        }

        if (
                server_name_length > 1024
                || book_native_timeout(
                        runtime,
                        arguments,
                        argument_count,
                        1,
                        &timeout_ms
                ) != NORMAL
        ) {
                return book_runtime_native_fail(
                        runtime,
                        "invalid TLS open arguments"
                );
        }

        name = calloc(server_name_length + 1U, 1);
        if (!name) {
                return book_runtime_native_fail(
                        runtime,
                        "failed to allocate TLS server name"
                );
        }

        if (server_name_length > 0) {
                memcpy(name, server_name, server_name_length);
        }

        status = book_tls_open(
                session,
                &state->tls,
                name,
                timeout_ms
        );

        memset(name, 0x00, server_name_length + 1U);
        free(name);

        if (
                book_native_status_value(
                        runtime,
                        status == NORMAL ? "ok" : "error",
                        &status_value
                ) != NORMAL
        ) {
                return ABNORMAL;
        }

        return book_native_return2(
                returns,
                book_runtime_boolean(
                        status == NORMAL ? ISTRUE : ISFALSE
                ),
                status_value
        );
}

static int book_native_tls_close_native(
        BOOK_RUNTIME * runtime,
        const BOOK_VALUE * arguments,
        c_size_t argument_count,
        BOOK_MULTI_VALUE * returns
) {
        BOOK_NATIVE_STATE * state;
        BOOK_VALUE status_value;

        (void)arguments;

        if (!runtime || !returns) return ABNORMAL;
        if (argument_count != 0) {
                return book_runtime_native_fail(
                        runtime,
                        "_kami.tls_close() takes no arguments"
                );
        }

        state = book_native_state(runtime);
        if (!state) return ABNORMAL;

        if (state->tls) book_tls_close(&state->tls);

        if (
                book_native_status_value(
                        runtime,
                        "ok",
                        &status_value
                ) != NORMAL
        ) {
                return ABNORMAL;
        }

        return book_native_return2(
                returns,
                book_runtime_boolean(ISTRUE),
                status_value
        );
}

static int book_native_tls_info(
        BOOK_RUNTIME * runtime,
        const BOOK_VALUE * arguments,
        c_size_t argument_count,
        BOOK_MULTI_VALUE * returns
) {
        BOOK_NATIVE_STATE * state;
        BOOK_VALUE info;

        (void)arguments;

        if (!runtime || !returns) return ABNORMAL;
        if (argument_count != 0) {
                return book_runtime_native_fail(
                        runtime,
                        "_kami.tls_info() takes no arguments"
                );
        }

        state = book_native_state(runtime);

        if (
                !state
                || book_runtime_make_table(runtime, &info) != NORMAL
                || book_native_table_set_boolean(
                        runtime,
                        info,
                        "active",
                        state->tls ? ISTRUE : ISFALSE
                ) != NORMAL
        ) {
                return ABNORMAL;
        }

        return book_native_return1(returns, info);
}

static int book_native_emit(
        BOOK_RUNTIME * runtime,
        const BOOK_VALUE * arguments,
        c_size_t argument_count,
        BOOK_MULTI_VALUE * returns
) {
        BOOK_SESSION * session;
        const unsigned char * key;
        const unsigned char * value;
        const unsigned char * color = (const unsigned char*)"default";
        c_size_t key_length;
        c_size_t value_length;
        c_size_t color_length = 7;
        char key_buffer[BOOK_OUTPUT_KEY_BLOCK];
        char color_buffer[32];
        book_output_color_t parsed_color;
        int status;

        if (!runtime || !returns) return ABNORMAL;
        session = book_runtime_session(runtime);

        if (
                !session
                || argument_count < 2
                || book_value_string(
                        &arguments[0],
                        &key,
                        &key_length
                ) != NORMAL
                || book_value_string(
                        &arguments[1],
                        &value,
                        &value_length
                ) != NORMAL
        ) {
                return book_runtime_native_fail(
                        runtime,
                        "_kami.emit() requires string key and value"
                );
        }

        if (argument_count > 2 && arguments[2].type != BOOK_VALUE_NIL) {
                if (
                        book_value_string(
                                &arguments[2],
                                &color,
                                &color_length
                        ) != NORMAL
                ) {
                        return book_runtime_native_fail(
                                runtime,
                                "emit color must be a string"
                        );
                }
        }

        if (
                key_length == 0
                || key_length >= sizeof(key_buffer)
                || color_length == 0
                || color_length >= sizeof(color_buffer)
        ) {
                return book_runtime_native_fail(
                        runtime,
                        "emit key or color is outside bounds"
                );
        }

        memset(key_buffer, 0x00, sizeof(key_buffer));
        memcpy(key_buffer, key, key_length);

        memset(color_buffer, 0x00, sizeof(color_buffer));
        memcpy(color_buffer, color, color_length);

        if (
                book_output_parse_color(
                        color_buffer,
                        &parsed_color
                ) != NORMAL
        ) {
                return book_runtime_native_fail(
                        runtime,
                        "emit color is invalid"
                );
        }

        status = book_output_emit(
                session,
                key_buffer,
                key_length,
                (const char*)value,
                value_length,
                parsed_color
        );

        if (status == BOOK_OUTPUT_LIMIT) {
                books_session_set_termination(
                        session,
                        BOOK_TERM_LIMIT_ERROR
                );
                return book_runtime_native_fail(
                        runtime,
                        "Book output limit exceeded"
                );
        }

        if (status != NORMAL) {
                return book_runtime_native_fail(
                        runtime,
                        "failed to stage Book output"
                );
        }

        return book_native_return1(
                returns,
                book_runtime_boolean(ISTRUE)
        );
}

static int book_native_arrow_notice(
        BOOK_RUNTIME * runtime,
        const BOOK_VALUE * arguments,
        c_size_t argument_count,
        BOOK_MULTI_VALUE * returns,
        const char * notice
) {
        const unsigned char * text;
        c_size_t length;

        if (
                !runtime
                || !returns
                || !notice
                || argument_count != 1
                || book_value_string(
                        &arguments[0],
                        &text,
                        &length
                ) != NORMAL
                || length > (c_size_t)INT_MAX
        ) {
                return book_runtime_native_fail(
                        runtime,
                        "Book arrow notice requires one bounded string"
                );
        }

        kui_add_line_and_render(
                "%s%.*s",
                notice,
                (int)length,
                text
        );

        return book_native_return1(
                returns,
                book_runtime_boolean(ISTRUE)
        );
}

static int book_native_inquiry(
        BOOK_RUNTIME * runtime,
        const BOOK_VALUE * arguments,
        c_size_t argument_count,
        BOOK_MULTI_VALUE * returns
) {
        return book_native_arrow_notice(
                runtime,
                arguments,
                argument_count,
                returns,
                NOTICE_TRANSMISSION
        );
}

static int book_native_complete(
        BOOK_RUNTIME * runtime,
        const BOOK_VALUE * arguments,
        c_size_t argument_count,
        BOOK_MULTI_VALUE * returns
) {
        return book_native_arrow_notice(
                runtime,
                arguments,
                argument_count,
                returns,
                NOTICE_RECIEVE
        );
}

static int book_native_notice(
        BOOK_RUNTIME * runtime,
        const BOOK_VALUE * arguments,
        c_size_t argument_count,
        BOOK_MULTI_VALUE * returns
) {
        const unsigned char * text;
        c_size_t length;

        if (
                !runtime
                || !returns
                || argument_count != 1
                || book_value_string(
                        &arguments[0],
                        &text,
                        &length
                ) != NORMAL
                || length > (c_size_t)INT_MAX
        ) {
                return book_runtime_native_fail(
                        runtime,
                        "_kami.notice() requires one bounded string"
                );
        }

        kui_add_line_and_render(
                NOTICE_INFO "%.*s",
                (int)length,
                text
        );

        return book_native_return1(
                returns,
                book_runtime_boolean(ISTRUE)
        );
}

static int book_native_bail(
        BOOK_RUNTIME * runtime,
        const BOOK_VALUE * arguments,
        c_size_t argument_count,
        BOOK_MULTI_VALUE * returns
) {
        BOOK_SESSION * session;
        const unsigned char * reason;
        c_size_t reason_length;
        char detail[BOOK_RUNTIME_ERROR_BLOCK];

        (void)returns;

        if (
                !runtime
                || argument_count != 1
                || book_value_string(
                        &arguments[0],
                        &reason,
                        &reason_length
                ) != NORMAL
        ) {
                return book_runtime_native_fail(
                        runtime,
                        "_kami.bail() requires one reason string"
                );
        }

        session = book_runtime_session(runtime);
        if (!session) return ABNORMAL;

        memset(detail, 0x00, sizeof(detail));
        if (reason_length >= sizeof(detail)) {
                reason_length = sizeof(detail) - 1U;
        }
        memcpy(detail, reason, reason_length);

        books_session_set_termination(
                session,
                BOOK_TERM_BAILED
        );
        books_audit_action(
                session,
                "BAIL",
                detail
        );

        return book_runtime_native_fail(
                runtime,
                detail[0] ? detail : "Book bailed"
        );
}

static int book_native_add_function(
        BOOK_RUNTIME * runtime,
        BOOK_VALUE table,
        const char * name,
        book_native_callback_t callback
) {
        BOOK_VALUE function;

        if (
                book_runtime_make_native(
                        runtime,
                        name,
                        callback,
                        &function
                ) != NORMAL
        ) {
                return ABNORMAL;
        }

        return book_runtime_table_set_string(
                runtime,
                table,
                name,
                function
        );
}

int book_native_private_table(
        BOOK_RUNTIME * runtime,
        BOOK_VALUE * value
) {
        BOOK_NATIVE_STATE * state;

        if (!runtime || !value) return ABNORMAL;
        state = book_native_state(runtime);
        if (!state) return ABNORMAL;

        *value = state->private_table;
        return NORMAL;
}

int book_native_install(BOOK_RUNTIME * runtime) {
        BOOK_NATIVE_STATE * state;
        BOOK_VALUE private_table;
        BOOK_VALUE target;

        if (!runtime) return ABNORMAL;
        if (book_runtime_native_state(runtime)) return NORMAL;

        state = calloc(1, sizeof(*state));
        if (!state) {
                return book_runtime_native_fail(
                        runtime,
                        "failed to allocate private Lua ABI state"
                );
        }

        book_runtime_set_native_state(runtime, state);

        if (
                book_runtime_make_table(
                        runtime,
                        &private_table
                ) != NORMAL
                || book_native_add_function(
                        runtime,
                        private_table,
                        "tcp_open",
                        book_native_tcp_open
                ) != NORMAL
                || book_native_add_function(
                        runtime,
                        private_table,
                        "udp_open",
                        book_native_udp_open
                ) != NORMAL
                || book_native_add_function(
                        runtime,
                        private_table,
                        "tx",
                        book_native_tx
                ) != NORMAL
                || book_native_add_function(
                        runtime,
                        private_table,
                        "rx",
                        book_native_rx
                ) != NORMAL
                || book_native_add_function(
                        runtime,
                        private_table,
                        "rx_exact",
                        book_native_rx_exact
                ) != NORMAL
                || book_native_add_function(
                        runtime,
                        private_table,
                        "tls_open",
                        book_native_tls_open_native
                ) != NORMAL
                || book_native_add_function(
                        runtime,
                        private_table,
                        "tls_close",
                        book_native_tls_close_native
                ) != NORMAL
                || book_native_add_function(
                        runtime,
                        private_table,
                        "tls_info",
                        book_native_tls_info
                ) != NORMAL
                || book_native_add_function(
                        runtime,
                        private_table,
                        "session_info",
                        book_native_session_info
                ) != NORMAL
                || book_native_add_function(
                        runtime,
                        private_table,
                        "emit",
                        book_native_emit
                ) != NORMAL
                || book_native_add_function(
                        runtime,
                        private_table,
                        "notice",
                        book_native_notice
                ) != NORMAL
                || book_native_add_function(
                        runtime,
                        private_table,
                        "inquiry",
                        book_native_inquiry
                ) != NORMAL
                || book_native_add_function(
                        runtime,
                        private_table,
                        "complete",
                        book_native_complete
                ) != NORMAL
                || book_native_add_function(
                        runtime,
                        private_table,
                        "bail",
                        book_native_bail
                ) != NORMAL
        ) {
                book_native_destroy(runtime);
                return ABNORMAL;
        }

        state->private_table = private_table;

        // @@ Public read-only-by-convention snapshot used by protocol modules.
        // Lua may mutate its own table copy; native session state is unaffected.
        if (
                book_native_target_table(
                        runtime,
                        &target
                ) != NORMAL
                || book_runtime_set_global(
                        runtime,
                        "target",
                        target
                ) != NORMAL
        ) {
                book_native_destroy(runtime);
                return ABNORMAL;
        }

        return NORMAL;
}

void book_native_destroy(BOOK_RUNTIME * runtime) {
        BOOK_NATIVE_STATE * state;

        if (!runtime) return;
        state = book_native_state(runtime);
        if (!state) return;

        if (state->tls) {
                book_tls_close(&state->tls);
        }

        memset(state, 0x00, sizeof(*state));
        free(state);
        book_runtime_set_native_state(runtime, NULL);
}
