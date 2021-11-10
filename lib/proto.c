/* Coding and decoding of quicrq API and messages
*/

#include <stdlib.h>
#include <string.h>
#include "quicrq.h"
#include "quicrq_internal.h"
#include "picoquic_utils.h"

/* The protocol defines a set of actions, identified by a code. For each action
 * we get a specific encoding and decoding function. We also use a generic decoding
 * structure.
 */

/* Media request message. 
 * 
 * quicrq_request_message {
 *     message_type(i),
 *     url_length(i),
 *     url(...)
 * }
 * 
 * Datagram variant:
 * 
 * quicrq_request_message {
 *     message_type(i),
 *     url_length(i),
 *     url(...),
 *     datagram_stream_id(i)
 * }
 * 
 * Same encoding and decoding code is used for both.
 * 
 */
size_t quicrq_rq_msg_reserve(size_t url_length)
{
    return 8 + 2 + url_length;
}

uint8_t* quicrq_rq_msg_encode(uint8_t* bytes, uint8_t* bytes_max, uint64_t message_type, size_t url_length, const uint8_t* url, uint64_t datagram_stream_id)
{
    if ((bytes = picoquic_frames_varint_encode(bytes, bytes_max, message_type)) != NULL &&
        (bytes = picoquic_frames_length_data_encode(bytes, bytes_max, url_length, url)) != NULL){
        if (message_type == QUICRQ_ACTION_OPEN_DATAGRAM) {
            bytes = picoquic_frames_varint_encode(bytes, bytes_max, datagram_stream_id);
        }
    }
    return bytes;
}

const uint8_t* quicrq_rq_msg_decode(const uint8_t* bytes, const uint8_t* bytes_max, uint64_t * message_type, size_t * url_length, const uint8_t** url, uint64_t* datagram_stream_id)
{
    *datagram_stream_id = 0;
    *url = NULL;
    *url_length = 0;
    if ((bytes = picoquic_frames_varint_decode(bytes, bytes_max, message_type)) != NULL &&
        (bytes = picoquic_frames_varlen_decode(bytes, bytes_max, url_length)) != NULL){
        *url = bytes;
        if ((bytes = picoquic_frames_fixed_skip(bytes, bytes_max, *url_length)) != NULL &&
            *message_type == QUICRQ_ACTION_OPEN_DATAGRAM) {
            bytes = picoquic_frames_varint_decode(bytes, bytes_max, datagram_stream_id);
        }
    }
    return bytes;
}

size_t quicrq_fin_msg_reserve(uint64_t final_frame_id)
{
#ifdef _WINDOWS
    UNREFERENCED_PARAMETER(final_frame_id);
#endif
    return 9;
}

/* Encoding or decoding the fin of datagram stream message
 * 
 * quicrq_fin_message {
 *     message_type(i),
 *     offset(i)
 */
uint8_t* quicrq_fin_msg_encode(uint8_t* bytes, uint8_t* bytes_max, uint64_t message_type, uint64_t final_frame_id)
{
    if ((bytes = picoquic_frames_varint_encode(bytes, bytes_max, message_type)) != NULL) {
        bytes = picoquic_frames_varint_encode(bytes, bytes_max, final_frame_id);
    }
    return bytes;
}

const uint8_t* quicrq_fin_msg_decode(const uint8_t* bytes, const uint8_t* bytes_max, uint64_t* message_type, uint64_t* final_frame_id)
{
    *final_frame_id = 0;
    if ((bytes = picoquic_frames_varint_decode(bytes, bytes_max, message_type)) != NULL) {
        bytes = picoquic_frames_varint_decode(bytes, bytes_max, final_frame_id);
    }
    return bytes;
}

size_t quicrq_repair_request_reserve(uint64_t repair_frame_id, uint64_t repair_offset, int is_last_segment, size_t repair_length)
{
#ifdef _WINDOWS
    UNREFERENCED_PARAMETER(repair_frame_id);
    UNREFERENCED_PARAMETER(repair_offset);
    UNREFERENCED_PARAMETER(is_last_segment);
    UNREFERENCED_PARAMETER(repair_length);
#endif
    return 1+8+8+8;
}

/* Encoding or decoding the repair request message
 *
 * quicrq_fin_message {
 *     message_type(i),
 *     offset(i),
 *     length(i)
 */
uint8_t* quicrq_repair_request_encode(uint8_t* bytes, uint8_t* bytes_max, uint64_t message_type, uint64_t repair_frame_id, uint64_t repair_offset, int is_last_segment, size_t repair_length)
{
    uint64_t offset_and_fin = (repair_offset << 1) | (uint64_t)(is_last_segment & 1);
    if ((bytes = picoquic_frames_varint_encode(bytes, bytes_max, message_type)) != NULL &&
        (bytes = picoquic_frames_varint_encode(bytes, bytes_max, repair_frame_id)) != NULL &&
        (bytes = picoquic_frames_varint_encode(bytes, bytes_max, offset_and_fin)) != NULL) {
        bytes = picoquic_frames_varint_encode(bytes, bytes_max, repair_length);
    }
    return bytes;
}

const uint8_t* quicrq_repair_request_decode(const uint8_t* bytes, const uint8_t* bytes_max, uint64_t* message_type, 
    uint64_t * repair_frame_id, uint64_t* repair_offset, int * is_last_segment, size_t* repair_length)
{
    uint64_t offset_and_fin = 0;
    *repair_frame_id = 0;
    *repair_offset = 0;
    *is_last_segment = 0;
    *repair_length = 0;
    if ((bytes = picoquic_frames_varint_decode(bytes, bytes_max, message_type)) != NULL &&
        (bytes = picoquic_frames_varint_decode(bytes, bytes_max, repair_frame_id)) != NULL &&
        (bytes = picoquic_frames_varint_decode(bytes, bytes_max, &offset_and_fin)) != NULL &&
        (bytes = picoquic_frames_varlen_decode(bytes, bytes_max, repair_length)) != NULL) {
        *repair_offset = (offset_and_fin >> 1);
        *is_last_segment = (int)(offset_and_fin & 1);
    }
    return bytes;
}

size_t quicrq_repair_msg_reserve(uint64_t repair_frame_id, uint64_t repair_offset, int is_last_segment, uint64_t repair_length)
{
#ifdef _WINDOWS
    UNREFERENCED_PARAMETER(repair_frame_id);
    UNREFERENCED_PARAMETER(repair_offset);
    UNREFERENCED_PARAMETER(is_last_segment);
#endif
    return 1 + 8 + 8 + 8 + repair_length;
}

/* Encoding or decoding the repair message
 *
 * quicrq_fin_message {
 *     message_type(i),
 *     offset(i),
 *     length(i),
 *     data(...)
 */
uint8_t* quicrq_repair_msg_encode(uint8_t* bytes, uint8_t* bytes_max, uint64_t message_type, uint64_t repair_frame_id, 
    uint64_t repair_offset, int is_last_segment, size_t repair_length, const uint8_t * repair_data)
{
    uint64_t offset_and_fin = (repair_offset << 1) | (uint64_t)(is_last_segment & 1);
    if ((bytes = picoquic_frames_varint_encode(bytes, bytes_max, message_type)) != NULL &&
        (bytes = picoquic_frames_varint_encode(bytes, bytes_max, repair_frame_id)) != NULL &&
        (bytes = picoquic_frames_varint_encode(bytes, bytes_max, offset_and_fin)) != NULL) {
        bytes = picoquic_frames_length_data_encode(bytes, bytes_max, repair_length, repair_data);
    }
    return bytes;
}

const uint8_t* quicrq_repair_msg_decode(const uint8_t* bytes, const uint8_t* bytes_max, uint64_t* message_type,
    uint64_t * repair_frame_id, uint64_t* repair_offset, int* is_last_segment, size_t * repair_length, const uint8_t ** repair_data)
{
    uint64_t offset_and_fin = 0;
    *repair_frame_id = 0;
    *repair_offset = 0;
    *is_last_segment = 0;
    *repair_length = 0;
    *repair_data = NULL;
    if ((bytes = picoquic_frames_varint_decode(bytes, bytes_max, message_type)) != NULL &&
        (bytes = picoquic_frames_varint_decode(bytes, bytes_max, repair_frame_id)) != NULL &&
        (bytes = picoquic_frames_varint_decode(bytes, bytes_max, &offset_and_fin)) != NULL &&
        (bytes = picoquic_frames_varlen_decode(bytes, bytes_max, repair_length)) != NULL){
        *repair_offset = (offset_and_fin >> 1);
        *is_last_segment = (int)(offset_and_fin & 1);
        *repair_data = bytes;
        bytes = picoquic_frames_fixed_skip(bytes, bytes_max, *repair_length);
    }
    return bytes;
}


/* Generic decoding of QUICRQ control message */
const uint8_t* quicrq_msg_decode(const uint8_t* bytes, const uint8_t* bytes_max, quicrq_message_t* msg)
{
    const uint8_t* bytes0 = bytes;
    memset(msg, 0, sizeof(quicrq_message_t));
    if ((bytes = picoquic_frames_varint_decode(bytes, bytes_max, &msg->message_type)) != NULL) {
        /* TODO: do not decode the message type twice */
        bytes = bytes0;
        switch (msg->message_type) {
        case QUICRQ_ACTION_OPEN_STREAM:
        case QUICRQ_ACTION_OPEN_DATAGRAM:
            bytes = quicrq_rq_msg_decode(bytes, bytes_max, &msg->message_type, &msg->url_length, &msg->url, &msg->datagram_stream_id);
            break;
        case QUICRQ_ACTION_FIN_DATAGRAM:
            bytes = quicrq_fin_msg_decode(bytes, bytes_max, &msg->message_type, &msg->frame_id);
            break;
        case QUICRQ_ACTION_REQUEST_REPAIR:
            bytes = quicrq_repair_request_decode(bytes, bytes_max, &msg->message_type, &msg->frame_id, &msg->offset, &msg->is_last_segment, &msg->length);
            break;
        case QUICRQ_ACTION_REPAIR:
            bytes = quicrq_repair_msg_decode(bytes, bytes_max, &msg->message_type, &msg->frame_id, &msg->offset, &msg->is_last_segment, &msg->length, &msg->data);
            break;
        default:
            /* Unexpected message type */
            bytes = NULL;
            break;
        }
    }
    return bytes;
}

/* Generic encoding of QUICRQ control message */
uint8_t* quicrq_msg_encode(uint8_t* bytes, uint8_t* bytes_max, quicrq_message_t* msg)
{
    switch (msg->message_type) {
    case QUICRQ_ACTION_OPEN_STREAM:
    case QUICRQ_ACTION_OPEN_DATAGRAM:
        bytes = quicrq_rq_msg_encode(bytes, bytes_max, msg->message_type, msg->url_length, msg->url, msg->datagram_stream_id);
        break;
    case QUICRQ_ACTION_FIN_DATAGRAM:
        bytes = quicrq_fin_msg_encode(bytes, bytes_max, msg->message_type, msg->frame_id);
        break;
    case QUICRQ_ACTION_REQUEST_REPAIR:
        bytes = quicrq_repair_request_encode(bytes, bytes_max, msg->message_type, msg->frame_id, msg->offset, msg->is_last_segment, msg->length);
        break;
    case QUICRQ_ACTION_REPAIR:
        bytes = quicrq_repair_msg_encode(bytes, bytes_max, msg->message_type, msg->frame_id, msg->offset, msg->is_last_segment, msg->length, msg->data);
        break;
    default:
        /* Unexpected message type */
        bytes = NULL;
        break;
    }
    return bytes;
}


/* encoding of the datagram header */
uint8_t* quicrq_datagram_header_encode(uint8_t* bytes, uint8_t* bytes_max, uint64_t datagram_stream_id,
    uint64_t frame_id, uint64_t frame_offset, int is_last_segment)
{
    uint64_t offset_and_fin = (frame_offset << 1) | (unsigned int)(is_last_segment & 1);
    if ((bytes = picoquic_frames_varint_encode(bytes, bytes_max, datagram_stream_id)) != NULL &&
        (bytes = picoquic_frames_varint_encode(bytes, bytes_max, frame_id)) != NULL) {
        bytes = picoquic_frames_varint_encode(bytes, bytes_max, offset_and_fin);
    }
    return bytes;
}

const uint8_t* quicrq_datagram_header_decode(const uint8_t* bytes, const uint8_t* bytes_max, uint64_t * datagram_stream_id,
    uint64_t* frame_id, uint64_t* frame_offset, int * is_last_segment)
{
    uint64_t offset_and_fin = 0;
    if ((bytes = picoquic_frames_varint_decode(bytes, bytes_max, datagram_stream_id)) != NULL &&
        (bytes = picoquic_frames_varint_decode(bytes, bytes_max, frame_id)) != NULL &&
        (bytes = picoquic_frames_varint_decode(bytes, bytes_max, &offset_and_fin)) != NULL) {
        *frame_offset = (offset_and_fin >> 1);
        *is_last_segment = (int)(offset_and_fin & 1);
    }
    return bytes;
}

/* Publish local source API.
 */

int quicrq_publish_source(quicrq_ctx_t * qr_ctx, uint8_t * url, size_t url_length, void* pub_ctx, quicrq_media_publisher_subscribe_fn subscribe_fn, quicrq_media_publisher_fn getdata_fn)
{
    int ret = 0;
    size_t source_ctx_size = sizeof(quicrq_media_source_ctx_t) + url_length;

    if (source_ctx_size < sizeof(quicrq_media_source_ctx_t)) {
        ret = -1;
    }
    else {
        quicrq_media_source_ctx_t* srce_ctx = (quicrq_media_source_ctx_t*)malloc(source_ctx_size);

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
    }

    return ret;
}

void quicrq_delete_source(quicrq_media_source_ctx_t* srce_ctx, quicrq_ctx_t* qr_ctx)
{
    quicrq_stream_ctx_t* stream_ctx = srce_ctx->first_stream;

    while (stream_ctx != NULL) {
        quicrq_stream_ctx_t* next_stream_ctx = stream_ctx->next_stream_for_source;
        
        stream_ctx->next_stream_for_source = NULL;
        stream_ctx->previous_stream_for_source = NULL;
        

        stream_ctx = next_stream_ctx;
    }

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
        /* Add stream to list of published streams */
        stream_ctx->media_source = srce_ctx;
        if (srce_ctx->last_stream == NULL) {
            srce_ctx->first_stream = stream_ctx;
            srce_ctx->last_stream = stream_ctx;
        }
        else {
            srce_ctx->last_stream->next_stream_for_source = stream_ctx;
            stream_ctx->previous_stream_for_source = srce_ctx->last_stream;
            srce_ctx->last_stream = stream_ctx;
        }
        /* Document media function. */
        stream_ctx->publisher_fn = srce_ctx->getdata_fn;
        /* Create a subscribe media context */
        stream_ctx->media_ctx = srce_ctx->subscribe_fn(url, url_length, srce_ctx->pub_ctx);
        if (stream_ctx->media_ctx == NULL) {
            ret = -1;
        }
        else if (stream_ctx->is_datagram) {
            stream_ctx->is_active_datagram = 1;
            picoquic_mark_datagram_ready(stream_ctx->cnx_ctx->cnx, 1);
        } else {
            picoquic_mark_active_stream(stream_ctx->cnx_ctx->cnx, stream_ctx->stream_id, 1, stream_ctx);
        }
    }
    return ret;
}

/* When data is available for a source, wake up the corresponding connection 
 * and possibly stream.
 * TODO: for datagram, we may want to manage a queue of media for which data is ready.
 */
void quicrq_source_wakeup(quicrq_media_source_ctx_t* srce_ctx)
{
    quicrq_stream_ctx_t* stream_ctx = srce_ctx->first_stream;
    while (stream_ctx != NULL) {
        if (stream_ctx->is_datagram) {
            picoquic_mark_datagram_ready(stream_ctx->cnx_ctx->cnx, 1);
            stream_ctx->is_active_datagram = 1;
        }
        else {
            picoquic_mark_active_stream(stream_ctx->cnx_ctx->cnx, stream_ctx->stream_id, 1, stream_ctx);
        }
        stream_ctx = stream_ctx->next_stream_for_source;
    }
};

/* Request media in connection.
 * Send a media request to the server.
 * TODO: mention datagram stream.
 */
int quicrq_cnx_subscribe_media(quicrq_cnx_ctx_t* cnx_ctx, uint8_t* url, size_t url_length,
    int use_datagrams, quicrq_media_consumer_fn media_consumer_fn, void* media_ctx)
{
    /* Create a stream for the media */
    int ret = 0;
    uint64_t stream_id = picoquic_get_next_local_stream_id(cnx_ctx->cnx, 0);
    quicrq_stream_ctx_t* stream_ctx = quicrq_create_stream_context(cnx_ctx, stream_id);
    quicrq_message_buffer_t* message = &stream_ctx->message_sent;

    if (stream_ctx == NULL) {
        ret = -1;
    }
    else {
        if (quicrq_msg_buffer_alloc(message, quicrq_rq_msg_reserve(url_length), 0) != 0) {
            ret = -1;
        }
        else {
            /* Format the media request */
            uint64_t datagram_stream_id = stream_ctx->cnx_ctx->next_datagram_stream_id;
            uint8_t* message_next = quicrq_rq_msg_encode(message->buffer, message->buffer + message->buffer_alloc,
                (use_datagrams)? QUICRQ_ACTION_OPEN_DATAGRAM:QUICRQ_ACTION_OPEN_STREAM, url_length, url, datagram_stream_id);
            if (message_next == NULL) {
                ret = -1;
            } else {
                /* Queue the media request message to that stream */
                stream_ctx->is_client = 1;
                stream_ctx->is_datagram = (use_datagrams != 0);
                stream_ctx->datagram_stream_id = datagram_stream_id;
                message->message_size = message_next - message->buffer;
                stream_ctx->consumer_fn = media_consumer_fn;
                stream_ctx->media_ctx = media_ctx;
                stream_ctx->send_state = quicrq_sending_initial;
                stream_ctx->receive_state = quicrq_receive_repair;
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