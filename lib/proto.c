/* Codingand decoding of quicrq APIand messages
 *
 * - Subscribe to segment
 * - Publish source
*/

#include <stdlib.h>
#include "quicrq.h"
#include "quicrq_internal.h"
#include "picoquic_utils.h"

/* Media request message. 
 * 
 * quicrq_message {
 *     message_type(i),
 *     url_length(i),
 *     url(...)
 * }
 */
size_t quicrq_rq_msg_reserved_length(size_t url_length)
{
    return 8 + 2 + url_length;
}

uint8_t* quicrq_rq_msg_encode(uint8_t* bytes, uint8_t* bytes_max, uint64_t message_type, size_t url_length, uint8_t* url)
{
    if ((bytes = picoquic_frames_varint_encode(bytes, bytes_max, message_type)) != NULL){
        bytes = picoquic_frames_length_data_encode(bytes, bytes_max, url_length, url);
    }
    return bytes;
}

const uint8_t* quicrq_rq_msg_decode(const uint8_t* bytes, const uint8_t* bytes_max, uint64_t * message_type, size_t * url_length, const uint8_t** url)
{
    if ((bytes = picoquic_frames_varint_decode(bytes, bytes_max, message_type)) != NULL &&
        (bytes = picoquic_frames_varlen_decode(bytes, bytes_max, url_length)) != NULL){
        *url = bytes;
        bytes = picoquic_frames_fixed_skip(bytes, bytes_max, *url_length);
    }
    return bytes;
}


/* Publish local source API.
 */

int quicrq_publish_source(quicrq_ctx_t * qr_ctx, uint8_t * url, size_t url_length, void* pub_ctx, quicrq_media_publisher_subscribe_fn subscribe_fn, quicrq_media_publisher_fn getdata_fn)
{
    int ret = 0;
    quicrq_media_source_ctx_t* srce_ctx = (quicrq_media_source_ctx_t*)malloc(sizeof(quicrq_media_source_ctx_t) + url_length);

    if (srce_ctx == NULL) {
        ret = -1;
    }
    else {
        memset(srce_ctx, 0, sizeof(quicrq_media_source_ctx_t));
        srce_ctx->media_url = ((uint8_t*)srce_ctx) + sizeof(quicrq_media_source_ctx_t);
        srce_ctx->media_url_length = url_length;
        memcpy(srce_ctx->media_url, url, url_length);
        if (qr_ctx->last_source == NULL) {
            qr_ctx->first_source = srce_ctx;
            qr_ctx->last_source = srce_ctx;
        }
        else {
            qr_ctx->last_source->next_source = srce_ctx;
            srce_ctx->previous_source = qr_ctx->last_source;
            qr_ctx->last_source = srce_ctx;
        }
        srce_ctx->pub_ctx = pub_ctx;
        srce_ctx->subscribe_fn = subscribe_fn;
        srce_ctx->getdata_fn = getdata_fn;
    }

    return ret;
}

void quicrq_delete_source(quicrq_media_source_ctx_t* srce_ctx, quicrq_ctx_t* qr_ctx)
{
    if (srce_ctx == qr_ctx->first_source) {
        qr_ctx->first_source = srce_ctx->next_source;
    }
    else {
        srce_ctx->next_source->previous_source = srce_ctx->next_source;
    }
    if (srce_ctx == qr_ctx->last_source) {
        qr_ctx->last_source = srce_ctx->previous_source;
    }
    else {
        srce_ctx->previous_source->next_source = srce_ctx->previous_source;
    }
    free(srce_ctx);
}

/* Parse incoming request, connect incoming stream to media source
 */
int quicrq_subscribe_local_media(quicrq_stream_ctx_t* stream_ctx, const uint8_t* url, const size_t url_length)
{
    int ret = 0;
    quicrq_ctx_t* qr_ctx = stream_ctx->cnx_ctx->qr_ctx;
    quicrq_media_source_ctx_t* srce_ctx = qr_ctx->first_source;

    /* Find whether there is a matching media published locally */
    while (srce_ctx != NULL) {
        if (url_length == srce_ctx->media_url_length &&
            memcmp(url, srce_ctx->media_url, url_length) == 0) {
            break;
        }
        srce_ctx = srce_ctx->next_source;
    }
    if (srce_ctx == NULL) {
        ret = -1;
    }
    else {
        /* Document media function. */
        stream_ctx->publisher_fn = srce_ctx->getdata_fn;
        /* Create a subscribe media context */
        stream_ctx->media_ctx = srce_ctx->subscribe_fn(url, url_length, srce_ctx->pub_ctx);
        if (stream_ctx->media_ctx == NULL) {
            ret = -1;
        }
        else {
            picoquic_mark_active_stream(stream_ctx->cnx_ctx->cnx, stream_ctx->stream_id, 1, stream_ctx);
        }
    }
    return ret;
}

/* Request media in connection.
 * Send a media request to the server.
 */
int quicrq_cnx_subscribe_media(quicrq_cnx_ctx_t* cnx_ctx, uint8_t* url, size_t url_length,
    quicrq_media_consumer_fn media_consumer_fn, void* media_ctx)
{
    /* Create a stream for the media */
    int ret = 0;
    uint64_t stream_id = picoquic_get_next_local_stream_id(cnx_ctx->cnx, 0);
    quicrq_stream_ctx_t* stream_ctx = quicrq_create_stream_context(cnx_ctx, stream_id);

    if (stream_ctx == NULL) {
        ret = -1;
    }
    else {
        if (quicrq_msg_buffer_alloc(&stream_ctx->message, quicrq_rq_msg_reserved_length(url_length), 0) != 0) {
            ret = -1;
        }
        else {
            /* Format the media request */
            uint8_t* message_next = quicrq_rq_msg_encode(stream_ctx->message.buffer, stream_ctx->message.buffer + stream_ctx->message.buffer_alloc,
                QUICRQ_ACTION_OPEN_STREAM, url_length, url);
            if (message_next == NULL) {
                ret = -1;
            } else {
                /* Queue the media request message to that stream */
                stream_ctx->is_client = 1;
                stream_ctx->message.message_size = message_next - stream_ctx->message.buffer;
                stream_ctx->consumer_fn = media_consumer_fn;
                stream_ctx->media_ctx = media_ctx;
                picoquic_mark_active_stream(cnx_ctx->cnx, stream_id, 1, stream_ctx);
            }
        }
    }
    return ret;
}

/* Request media API -- connection independent.
 * - If the media is known locally, call "init media" when this message is received.
 * - If not, open a stream context, pass message to server, pass incoming media to subscriber.
 * To be developed later, when we develop the relay.
 */
int quicrq_subscribe_media(quicrq_ctx_t* qr_ctx, uint8_t* url, uint8_t* url_length)
{
    return -1;
}