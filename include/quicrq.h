#ifndef QUICRQ_H
#define QUICRQ_H

#include <stdint.h>
#include <picoquic.h>

#ifdef __cplusplus
extern "C" {
#endif

/* QUICR ALPN and QUICR port -- as defined in draft */
#define QUICRQ_ALPN "quicr-h00"
#define QUICRQ_PORT 853

/* QUICR error codes */
#define QUICRQ_ERROR_NO_ERROR 0x00
#define QUICRQ_ERROR_INTERNAL 0x01
#define QUICRQ_ERROR_PROTOCOL 0x02

/* QUICR client return codes
*/
typedef enum {
    quicrq_incoming_query = 0, /* Incoming callback query */
    quicrq_query_cancelled, /* Query cancelled before response provided */
    quicrq_response_complete, /* The last response to the current query arrived. */
    quicrq_response_partial, /* One of the first responses to a query has arrived */
    quicrq_response_cancelled, /* The response to the current query was cancelled by the peer. */
    quicrq_query_failed  /* Query failed for reasons other than cancelled. */
} quicrq_query_return_enum;


/* Connection context management functions.
 * The type quicrq_ctx_t is treated here as an opaque pointer, to
 * provide isolation between the app and the stack.
 */

typedef struct st_quicrq_ctx_t quicrq_ctx_t;
typedef struct st_quicrq_cnx_ctx_t quicrq_cnx_ctx_t;

quicrq_ctx_t* quicrq_create(char const* alpn,
    char const* cert_file_name, char const* key_file_name, char const* cert_root_file_name,
    char const* ticket_store_file_name, char const* token_store_file_name,
    const uint8_t* ticket_encryption_key, size_t ticket_encryption_key_length,
    uint64_t* simulated_time);
void quicrq_delete(quicrq_ctx_t* ctx);
picoquic_quic_t* quicrq_get_quic_ctx(quicrq_ctx_t* ctx);

quicrq_cnx_ctx_t* quicrq_create_cnx_context(quicrq_ctx_t* qr_ctx, picoquic_cnx_t* cnx);
void quicrq_delete_cnx_context(quicrq_cnx_ctx_t* cnx_ctx);

#ifdef __cplusplus
}
#endif


#endif
