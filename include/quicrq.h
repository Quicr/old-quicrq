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
typedef struct st_quicrq_stream_ctx_t quicrq_stream_ctx_t;

quicrq_ctx_t* quicrq_create(char const* alpn,
    char const* cert_file_name, char const* key_file_name, char const* cert_root_file_name,
    char const* ticket_store_file_name, char const* token_store_file_name,
    const uint8_t* ticket_encryption_key, size_t ticket_encryption_key_length,
    uint64_t* simulated_time);
void quicrq_delete(quicrq_ctx_t* ctx);
picoquic_quic_t* quicrq_get_quic_ctx(quicrq_ctx_t* ctx);

quicrq_cnx_ctx_t* quicrq_create_cnx_context(quicrq_ctx_t* qr_ctx, picoquic_cnx_t* cnx);
void quicrq_delete_cnx_context(quicrq_cnx_ctx_t* cnx_ctx);

/* Media stream definition.
 * Media is composed of series of frames, frames have
 * headers and content. Header provides information
 * about sufficient for synchronization and replay
 */
typedef struct st_quicrq_media_frame_header_t {
    uint64_t timestamp; /* time from start of media segment */
    uint64_t number; /* start at 1 for media segment */
    size_t length; /* number of content bytes */
} quicrq_media_frame_header_t;

/* Media publisher API.
 * Simplified API for now:
 * - qr_ctx: QUICR context in which the media is published
 * - media_url: URL of the media segment
 * - media_publisher_cb: callback function for processing media arrival
 * - media_ctx: media context managed by the publisher
 */

typedef int (*quicr_media_publisher_cb)(
    void* media_ctx,
    uint8_t* data,
    size_t data_max_size,
    size_t* data_length,
    uint64_t* next_publication_time);

int quicrq_publish_media_stream(
    quicrq_ctx_t* qr_ctx,
    char const* url,
    quicr_media_publisher_cb media_consumer_cb,
    void* media_ctx);

/* Subscribe to a media segment using QUIC streams.
 * Simplified API for now:
 * - cnx_ctx: context of the QUICR connection
 * - media_url: URL of the media segment
 * - media_consumer_cb: callback function for processing media arrival
 * - media_ctx: media context managed by the application
 */

typedef int (*quicr_media_consumer_cb)(
    void* media_ctx,
    quicrq_cnx_ctx_t* cnx_ctx,
    quicrq_stream_ctx_t* stream_ctx,
    uint8_t* data, size_t data_length);

int quicrq_subscribe_media_stream(
    quicrq_cnx_ctx_t* cnx_ctx,
    char const* url,
    quicr_media_consumer_cb media_consumer_fn,
    void* media_ctx);

#ifdef __cplusplus
}
#endif


#endif
