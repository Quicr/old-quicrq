/*
 * QUICR-Q:
 * 
 * Prototyping of quic real-time on top of picoquic.
 * 
 * The prototype will implement several variations of QUICR: stream, rush, and datagrams.
 * These variants use common "glue" code to interface with picoquic:
 *  - feeding media frames for transmission
 *  - providing media frames for rendering
 *  - implementing the picoquic callback
 *  - implementing the socket loop used by picoquic.
 * The socket loop is adapted to wait for media input or end of rendering as well as packet arrival.
 * 
 * The library can be used in three context:
 * - To implement an "origin" server
 * - To implement a relay (e.g., CDN relay)
 * - To implement a client.
 * 
 * The main transaction is the retrieval of a media stream from a server. In the
 * test implementation, this is done by the client setting a connection to the relay
 * (or reusing a suitable connection), and then queuing a "media segment request", to
 * be sent on the first available client stream. The media request specifies,
 * at a minimum, the identification of the media, possibly the time to start
 * the replay, and the retrieval variant, e.g. stream or datagram.
 * 
 * If the media is available (e.g. at origin), it is sent immediately.
 * If not, the request is queued and the media is requested to an upstream server.
 */

#include <stdlib.h>
#include <string.h>
#include "picoquic_utils.h"
#include "quicrq.h"
#include "quicrq_internal.h"

/* Allocate space in the message buffer */
int quicrq_msg_buffer_alloc(quicrq_message_buffer_t* msg_buffer, size_t space, size_t bytes_stored)
{
    int ret = 0;

    if (space > msg_buffer->buffer_alloc) {
        uint8_t* x = (uint8_t*)malloc(space);
        if (x == NULL) {
            /* internal error! */
            ret = -1;
        }
        else {
            if (bytes_stored > 0) {
                memcpy(x, msg_buffer->buffer, bytes_stored);
            }
            free(msg_buffer->buffer);
            msg_buffer->buffer_alloc = space;
            msg_buffer->buffer = x;
        }
    }
    return ret;
}

/* Accumulate a protocol message from series of read data call backs */
uint8_t* quicrq_msg_buffer_store(uint8_t* bytes, size_t length, quicrq_message_buffer_t* msg_buffer, int* is_finished)
{
    *is_finished = 0;

    while (msg_buffer->nb_bytes_read < 2 && length > 0) {
        msg_buffer->nb_bytes_read++;
        msg_buffer->message_size *= 8;
        msg_buffer->message_size += bytes[0];
        bytes++;
        length--;
    }

    if (msg_buffer->nb_bytes_read >= 2) {
        size_t bytes_stored = msg_buffer->nb_bytes_read - 2;
        size_t required = msg_buffer->message_size - bytes_stored;

        if (required > 0) {
            if (quicrq_msg_buffer_alloc(msg_buffer, msg_buffer->message_size, bytes_stored) != 0) {
                bytes = NULL;
            } else {
                if (length <= required) {
                    length = required;
                    *is_finished = 1;
                }
                memcpy(msg_buffer->buffer + bytes_stored, bytes, length);
                bytes += length;
                msg_buffer->nb_bytes_read += length;
            }
        }
        else {
            *is_finished = 1;
        }
    }

    return bytes;
}

/* Send a protocol message through series of read data call backs */
int quicrq_msg_buffer_prepare_to_send(quicrq_stream_ctx_t* stream_ctx, void* context, size_t space)
{
    int ret = 0;
    quicrq_message_buffer_t* msg_buffer = &stream_ctx->message;
    size_t total_to_send = msg_buffer->message_size + 2;

    if (msg_buffer->nb_bytes_read < total_to_send) {
        uint8_t* buffer;
        size_t available = total_to_send - msg_buffer->nb_bytes_read;
        int is_fin = 1;

        if (available > space) {
            available = space;
            is_fin = 0;
        }

        buffer = picoquic_provide_stream_data_buffer(context, available, is_fin, !is_fin);
        if (buffer != NULL) {
            /* Feed the message length on two bytes */
            while (msg_buffer->nb_bytes_read < 2 && available > 0) {
                uint8_t b = (msg_buffer->nb_bytes_read == 0) ?
                    (uint8_t)((msg_buffer->message_size >> 8) & 255) :
                    (uint8_t)(msg_buffer->message_size & 255);
                *buffer = b;
                buffer++;
                available--;
                msg_buffer->nb_bytes_read++;
            }
            /* feed the content at offset */
            if (available > 0) {
                size_t offset = msg_buffer->nb_bytes_read - 2;
                memcpy(buffer, msg_buffer->buffer + offset, available);
                msg_buffer->nb_bytes_read += available;
                stream_ctx->is_client_finished = (is_fin != 0);
            }
        }
    }
    return ret;
}

/* send the media using the provider supplied function */
int quicrq_prepare_to_send_media(quicrq_stream_ctx_t* stream_ctx, void* context, size_t space, uint64_t current_time)
{
    /* Find how much data is available on the media stream */
    int is_finished = 0;
    size_t available = 0;
    size_t data_length = 0;
    int ret = stream_ctx->publisher_fn(quicrq_media_source_get_data, stream_ctx->media_ctx, NULL, space, &available, &is_finished, current_time);
    /* Ask the media stream to fill the buffer.
     * Do this even if the stream is finished and there is no data to send, as this is a way
     * to communicate the FIN of stream to the stack.
     */
    if (ret == 0) {
        void * buffer = picoquic_provide_stream_data_buffer(context, available, is_finished, !is_finished);
        if (buffer == NULL) {
            ret = -1;
        }
        else {
            ret = stream_ctx->publisher_fn(quicrq_media_source_get_data, stream_ctx->media_ctx, buffer, available, &data_length, &is_finished, current_time);
            if (ret == 0 && available != data_length) {
                ret = -1;
            }
            else if (is_finished) {
                stream_ctx->is_server_finished = 1;
                /* TODO: Should at this point close the publishing service */
            }
        }
    }
    return ret;
}

/* Receive data in a datagram */
int quicrq_receive_datagram(quicrq_cnx_ctx_t* cnx_ctx, const uint8_t* bytes, size_t length, uint64_t current_time)
{
    int ret = 0;
    quicrq_stream_ctx_t* stream_ctx = NULL;

    /* Parse the datagram header */
    const uint8_t* bytes_max = bytes + length;
    uint64_t datagram_stream_id;
    uint64_t datagram_offset;
    const uint8_t* next_bytes;

    next_bytes = quicrq_datagram_header_decode(bytes, bytes_max, &datagram_stream_id, &datagram_offset);
    if (next_bytes == NULL) {
        ret = -1;
    }
    else {
        /* Find the stream context by datagram ID */
        stream_ctx = cnx_ctx->first_stream;
        while (stream_ctx != NULL) {
            if (stream_ctx->is_client && stream_ctx->is_datagram && stream_ctx->datagram_stream_id == datagram_stream_id) {
                break;
            }
            stream_ctx = stream_ctx->next_stream;
        }
        if (stream_ctx == NULL) {
            ret = -1;
        }
        else {
            /* Pass data to the media context. Consider handling the offset. */
            ret = stream_ctx->consumer_fn(quicrq_media_datagram_ready, stream_ctx->media_ctx, current_time, next_bytes, datagram_offset, bytes_max - next_bytes);
        }
    }

    return ret;
}

/* Prepare to send a datagram */

int quicrq_prepare_to_send_datagram(quicrq_cnx_ctx_t* cnx_ctx, void* context, size_t space, uint64_t current_time)
{
    /* Find a stream on which datagrams are available */
    int ret = 0;
    int at_least_one_active = 0;
    quicrq_stream_ctx_t* stream_ctx = cnx_ctx->first_stream;

    while (stream_ctx != NULL) {
        if (stream_ctx->is_datagram && !stream_ctx->is_client && stream_ctx->is_active_datagram) {
            /* Check how much data is ready */
            int is_finished = 0;
            size_t available = 0;
            size_t data_length = 0;
            /* Compute length of datagram_stream_id + length of offset */
            uint8_t datagram_header[QUICRQ_DATAGRAM_HEADER_MAX];
            uint8_t* h_byte = quicrq_datagram_header_encode(datagram_header, datagram_header + QUICRQ_DATAGRAM_HEADER_MAX, stream_ctx->datagram_stream_id, stream_ctx->highest_offset);
            size_t h_size;
            if (h_byte == NULL) {
                ret = -1;
                break;
            }
            h_size = h_byte - datagram_header;
            if (h_size > space) {
                /* TODO: should get a min encoding length per stream */
                /* Can't do anything there */
                at_least_one_active = 1;
            } else {
                ret = stream_ctx->publisher_fn(quicrq_media_source_get_data, stream_ctx->media_ctx, NULL, space - h_size, &available, &is_finished, current_time);
                /* Get a buffer inside the datagram packet */
                if (ret == 0){
                    if (is_finished) {
                        /* Mark the stream as finished */
                        /* Consider how to send an end-of-stream mark to the peer, maybe on the control flow. */
                        stream_ctx->is_active_datagram = 0;
                        stream_ctx->final_offset = stream_ctx->highest_offset + available;
                        /* Wake up the control stream so the final message can be sent. */
                        picoquic_mark_active_stream(stream_ctx->cnx_ctx->cnx, stream_ctx->stream_id, 1, stream_ctx);
                        stream_ctx->is_active_datagram = 0;
                    }
                    if (available > 0) {
                        void* buffer = picoquic_provide_datagram_buffer(context, available + h_size);
                        at_least_one_active = 1;
                        if (buffer == NULL) {
                            ret = -1;
                        }
                        else {
                            /* Push the header */
                            memcpy(buffer, datagram_header, h_size);
                            /* Get the media */
                            ret = stream_ctx->publisher_fn(quicrq_media_source_get_data, stream_ctx->media_ctx, ((uint8_t*)buffer) + h_size, available, &data_length, &is_finished, current_time);
                            if (ret == 0 && available != data_length) {
                                ret = -1;
                            }
                            /* Update offset based on what is sent. */
                            stream_ctx->highest_offset += available;
                        }
                        /* Exit the loop, since data was copied */
                        break;
                    }
                    else {
                        stream_ctx->is_active_datagram = 0;
                    }
                }
            }
        }
        stream_ctx = stream_ctx->next_stream;
    }

    if (ret == 0) {
        picoquic_mark_datagram_ready(cnx_ctx->cnx, at_least_one_active);
    }

    return ret;
}
/* Receive and process media control messages */
int quicrq_receive_server_response(quicrq_stream_ctx_t* stream_ctx, uint8_t* bytes, size_t length, int is_fin)
{
    int ret = 0;

    if (stream_ctx->is_client_finished && length > 0) {
        /* One message per stream! */
        ret = -1;
    }
    else if (length > 0) {
        int is_finished = 0;
        uint8_t* next_bytes = quicrq_msg_buffer_store(bytes, length, &stream_ctx->message, &is_finished);
        if (next_bytes == NULL) {
            ret = -1;
        }
        else if (next_bytes != bytes + length) {
            /* we only expect one message per stream. This is a protocol violation. */
            ret = -1;
        }
        else if (is_finished) {
            /* Process the media command */
            uint64_t message_type;
            uint64_t final_offset = 0;
            const uint8_t* next_bytes = quicrq_fin_msg_decode(stream_ctx->message.buffer, stream_ctx->message.buffer + stream_ctx->message.message_size,
                &message_type, &final_offset);

            if (next_bytes == NULL) {
                /* bad message format */
                ret = -1;
            }
            else {
                /* Mark message as received */
                stream_ctx->is_server_finished = 1;
                /* Signal final offset to receiver */

                ret = stream_ctx->consumer_fn(quicrq_media_final_offset, stream_ctx->media_ctx, picoquic_get_quic_time(stream_ctx->cnx_ctx->qr_ctx->quic), NULL, final_offset, 0);
            }
        }
    }

    if (is_fin) {
        /* end of command stream. If something is in progress, yell */
        if (!stream_ctx->is_client_finished) {
            ret = -1;
        }
    }

    return ret;
}

/* Receive and process media control messages */
int quicrq_receive_server_command(quicrq_stream_ctx_t* stream_ctx, uint8_t* bytes, size_t length, int is_fin)
{
    int ret = 0;

    if (stream_ctx->is_client_finished && length > 0) {
        /* One message per stream! */
        ret = -1;
    } else if (length > 0) {
        int is_finished = 0;
        uint8_t* next_bytes = quicrq_msg_buffer_store(bytes, length, &stream_ctx->message, &is_finished);
        if (next_bytes == NULL) {
            ret = -1;
        }
        else if (next_bytes != bytes + length) {
            /* we only expect one message per stream. This is a protocol violation. */
            ret = -1;
        }
        else if (is_finished) {
            /* Process the media command */
            uint64_t message_type;
            size_t url_length = 0;
            const uint8_t* url;
            const uint8_t* next_bytes = quicrq_rq_msg_decode(stream_ctx->message.buffer, stream_ctx->message.buffer + stream_ctx->message.message_size,
                &message_type, &url_length, &url, &stream_ctx->datagram_stream_id);

            if (next_bytes == NULL) {
                /* bad message format */
                ret = -1;
            }
            else {
                /* Mark message as received */
                stream_ctx->is_client_finished = 1;
                stream_ctx->is_datagram = (message_type == QUICRQ_ACTION_OPEN_DATAGRAM);
                /* Open the media -- TODO, variants with different actions. */
                ret = quicrq_subscribe_local_media(stream_ctx, url, url_length);
            }
        }
    }

    if (is_fin) {
        /* end of command stream. If something is in progress, yell */
        if (!stream_ctx->is_client_finished) {
            ret = -1;
        }
    }

    return ret;
}

/* Callback from Quic
 */
int quicrq_callback(picoquic_cnx_t* cnx,
    uint64_t stream_id, uint8_t* bytes, size_t length,
    picoquic_call_back_event_t fin_or_event, void* callback_ctx, void* v_stream_ctx)
{

    int ret = 0;
    quicrq_cnx_ctx_t* cnx_ctx = (quicrq_cnx_ctx_t*)callback_ctx;
    quicrq_stream_ctx_t* stream_ctx = (quicrq_stream_ctx_t*)v_stream_ctx;

    /* If this is the first reference to the connection, the application context is set
     * to the default value defined for the server. This default value contains the pointer
     * to the global context in which streams and roles are defined.
     */
    if (callback_ctx == NULL || callback_ctx == picoquic_get_default_callback_context(picoquic_get_quic_ctx(cnx))) {
        if (fin_or_event == picoquic_callback_close) {
            picoquic_set_callback(cnx, NULL, NULL);
            return 0;
        }
        else {
            cnx_ctx = quicrq_create_cnx_context((quicrq_ctx_t*)callback_ctx, cnx);
            if (cnx_ctx == NULL) {
                /* cannot handle the connection */
                picoquic_close(cnx, PICOQUIC_ERROR_MEMORY);
                return -1;
            }
            else {
                picoquic_set_callback(cnx, quicrq_callback, cnx_ctx);
            }
        }
    }

    if (ret == 0) {
        switch (fin_or_event) {
        case picoquic_callback_stream_data:
        case picoquic_callback_stream_fin:
            /* Data arrival on stream #x, maybe with fin mark */
            if (stream_ctx == NULL) {
                /* Create and initialize stream context */
                stream_ctx = quicrq_create_stream_context(cnx_ctx, stream_id);
            }

            if (stream_ctx == NULL) {
                /* Internal error */
                (void)picoquic_reset_stream(cnx, stream_id, QUICRQ_ERROR_INTERNAL);
                return(-1);
            }
            else if (stream_ctx->is_client) {
                if (!stream_ctx->is_datagram) {
                    /* In the basic protocol, the client receives media data */
                    ret = stream_ctx->consumer_fn(quicrq_media_data_ready, stream_ctx->media_ctx, picoquic_get_quic_time(stream_ctx->cnx_ctx->qr_ctx->quic), bytes, stream_ctx->highest_offset, length);
                    stream_ctx->highest_offset += length;
                    if (ret == 0 && fin_or_event == picoquic_callback_stream_fin && !stream_ctx->is_server_finished) {
                        stream_ctx->is_server_finished = 1;
                        ret = stream_ctx->consumer_fn(quicrq_media_final_offset, stream_ctx->media_ctx, picoquic_get_quic_time(stream_ctx->cnx_ctx->qr_ctx->quic), NULL, stream_ctx->highest_offset, 0);
                    }
                }
                else {
                    /* In the basic protocol, the server may send messages */
                    ret = quicrq_receive_server_response(stream_ctx, bytes, length, (fin_or_event == picoquic_callback_stream_fin));
                }
            }
            else {
                /* In the basic protocol, the server receives messages */
                ret = quicrq_receive_server_command(stream_ctx, bytes, length, (fin_or_event == picoquic_callback_stream_fin));
            }
            if (stream_ctx->is_client_finished && stream_ctx->is_server_finished) {
                quicrq_delete_stream_ctx(cnx_ctx, stream_ctx);
                stream_ctx = NULL;
            }
            break;
        case picoquic_callback_prepare_to_send:
            /* Active sending API */
            if (stream_ctx == NULL) {
                /* This should never happen */
                ret = -1;
            }
            else if (stream_ctx->is_client) {
                /* In the basic protocol, the client sends request messages, followed by fin */
                ret = quicrq_msg_buffer_prepare_to_send(stream_ctx, bytes, length);
            }
            else if (!stream_ctx->is_datagram) {
                /* In the basic protocol, the server sends data from a source */
                ret = quicrq_prepare_to_send_media(stream_ctx, bytes, length, picoquic_get_quic_time(stream_ctx->cnx_ctx->qr_ctx->quic));
            }
            else {
                /* In the datagram protocol, the server sends a closing message */
                /* TODO */
            }
            if (stream_ctx->is_client_finished && stream_ctx->is_server_finished) {
                quicrq_delete_stream_ctx(cnx_ctx, stream_ctx);
                stream_ctx = NULL;
            }
            break;
        case picoquic_callback_datagram:
            /* Receive data in a datagram */
            ret = quicrq_receive_datagram(cnx_ctx, bytes, length, picoquic_get_quic_time(cnx_ctx->qr_ctx->quic));
            break;
        case picoquic_callback_prepare_datagram:
            /* Prepare to send a datagram */
            ret = quicrq_prepare_to_send_datagram(cnx_ctx, bytes, length, picoquic_get_quic_time(cnx_ctx->qr_ctx->quic));
            break;
        case picoquic_callback_stream_reset: /* Client reset stream #x */
        case picoquic_callback_stop_sending: /* Client asks server to reset stream #x */
#if 0
            /* TODO: react to abandon stream, etc. */
            if (stream_ctx != NULL) {
                /* Mark stream as abandoned, close the file, etc. */
                sample_server_delete_stream_context(cnx_ctx, stream_ctx);
                picoquic_reset_stream(cnx, stream_id, PICOQUIC_SAMPLE_FILE_CANCEL_ERROR);
            }
#endif
            break;
        case picoquic_callback_stateless_reset: /* Received an error message */
        case picoquic_callback_close: /* Received connection close */
        case picoquic_callback_application_close: /* Received application close */
            /* Remove the connection from the context, and then delete it */
            cnx_ctx->cnx = NULL;
            quicrq_delete_cnx_context(cnx_ctx);
            picoquic_set_callback(cnx, NULL, NULL);
            break;
        case picoquic_callback_version_negotiation:
            /* The server should never receive a version negotiation response */
            break;
        case picoquic_callback_stream_gap:
            /* This callback is never used. */
            break;
        case picoquic_callback_almost_ready:
        case picoquic_callback_ready:
            /* Check that the transport parameters are what the sample expects */
            break;
        default:
            /* unexpected */
            break;
        }
    }

    return ret;
}

/* get the quic context from quicqr context */
picoquic_quic_t* quicrq_get_quic_ctx(quicrq_ctx_t* qr_ctx)
{
    return (qr_ctx==NULL)?NULL:qr_ctx->quic;
}

/* Delete a QUICR configuration */
void quicrq_delete(quicrq_ctx_t* qr_ctx)
{
    struct st_quicrq_cnx_ctx_t* cnx_ctx = qr_ctx->first_cnx;
    struct st_quicrq_cnx_ctx_t* next = NULL;

    while (cnx_ctx != NULL) {
        next = cnx_ctx->next_cnx;
        quicrq_delete_cnx_context(cnx_ctx);
        cnx_ctx = next;
    }

    if (qr_ctx->quic != NULL) {
        picoquic_free(qr_ctx->quic);
    }

    free(qr_ctx);
}

/* Create a QUICRQ context
 * TODO: consider passing a picoquic configuration object 
 */
quicrq_ctx_t* quicrq_create(char const* alpn,
    char const* cert_file_name, char const* key_file_name, char const* cert_root_file_name,
    char const* ticket_store_file_name, char const* token_store_file_name,
    const uint8_t* ticket_encryption_key, size_t ticket_encryption_key_length,
    uint64_t* p_simulated_time)
{
    quicrq_ctx_t* qr_ctx = (quicrq_ctx_t*)malloc(sizeof(quicrq_ctx_t));
    uint64_t current_time = (p_simulated_time == NULL) ? picoquic_current_time() : *p_simulated_time;

    if (qr_ctx != NULL) {
        memset(qr_ctx, 0, sizeof(quicrq_ctx_t));

        qr_ctx->quic = picoquic_create(QUICRQ_MAX_CONNECTIONS, cert_file_name, key_file_name, cert_root_file_name, alpn,
            quicrq_callback, qr_ctx, NULL, NULL, NULL, current_time, p_simulated_time,
            ticket_store_file_name, ticket_encryption_key, ticket_encryption_key_length);

        if (qr_ctx->quic == NULL ||
            (token_store_file_name != NULL && picoquic_load_retry_tokens(qr_ctx->quic, token_store_file_name) != 0)) {
            quicrq_delete(qr_ctx);
            qr_ctx = NULL;
        }
    }
    return qr_ctx;
}

/* Delete a connection context */
void quicrq_delete_cnx_context(quicrq_cnx_ctx_t* cnx_ctx)
{
    /* Delete the quic connection */
    if (cnx_ctx->cnx != NULL) {
        picoquic_set_callback(cnx_ctx->cnx, NULL, NULL);
        picoquic_delete_cnx(cnx_ctx->cnx);
        cnx_ctx->cnx = NULL;
    }
    /* Delete the stream contexts */
    while (cnx_ctx->first_stream != NULL) {
        quicrq_delete_stream_ctx(cnx_ctx, cnx_ctx->first_stream);
    }
    /* Remove the connection from the double linked list */
    if (cnx_ctx->qr_ctx != NULL) {
        if (cnx_ctx->next_cnx == NULL) {
            cnx_ctx->qr_ctx->last_cnx = cnx_ctx->previous_cnx;
        }
        else {
            cnx_ctx->next_cnx->previous_cnx = cnx_ctx->previous_cnx;
        }
        if (cnx_ctx->previous_cnx == NULL) {
            cnx_ctx->qr_ctx->first_cnx = cnx_ctx->next_cnx;
        }
        else {
            cnx_ctx->previous_cnx->next_cnx = cnx_ctx->next_cnx;
        }
    }
    /* Free the context */
    free(cnx_ctx);
}

/* Create a connection context. 
 * The QUIC connection has to be created before the QUICRQ connection. */
quicrq_cnx_ctx_t* quicrq_create_cnx_context(quicrq_ctx_t* qr_ctx, picoquic_cnx_t * cnx)
{
    quicrq_cnx_ctx_t* cnx_ctx = (quicrq_cnx_ctx_t*)malloc(sizeof(quicrq_cnx_ctx_t));

    if (cnx_ctx != NULL) {
        memset(cnx_ctx, 0, sizeof(quicrq_cnx_ctx_t));
        /* document quic connection */
        cnx_ctx->cnx = cnx;
        /* Add the connection in the double linked list */
        if (qr_ctx->last_cnx == NULL) {
            qr_ctx->first_cnx = cnx_ctx;
        }
        else {
            qr_ctx->last_cnx->next_cnx = cnx_ctx;
        }
        cnx_ctx->previous_cnx = qr_ctx->last_cnx;
        qr_ctx->last_cnx = cnx_ctx;
        cnx_ctx->qr_ctx = qr_ctx;
        picoquic_set_callback(cnx, quicrq_callback, cnx_ctx);
    }
    return cnx_ctx;
}

void quicrq_delete_stream_ctx(quicrq_cnx_ctx_t* cnx_ctx, quicrq_stream_ctx_t* stream_ctx)
{
    if (stream_ctx->next_stream == NULL) {
        cnx_ctx->last_stream = stream_ctx->previous_stream;
    }
    else {
        stream_ctx->next_stream->previous_stream = stream_ctx->previous_stream;
    }
    if (stream_ctx->previous_stream == NULL) {
        cnx_ctx->first_stream = stream_ctx->next_stream;
    }
    else {
        stream_ctx->previous_stream->next_stream = stream_ctx->next_stream;
    }
    if (cnx_ctx->cnx != NULL) {
        (void)picoquic_mark_active_stream(cnx_ctx->cnx, stream_ctx->stream_id, 0, NULL);
    }
    if (stream_ctx->media_ctx != NULL) {
        if (stream_ctx->is_client) {
            stream_ctx->consumer_fn(quicrq_media_close, stream_ctx->media_ctx, 0, NULL, 0, 0);
        }
        else {
            stream_ctx->publisher_fn(quicrq_media_source_close, stream_ctx->media_ctx, NULL, 0, NULL, NULL, 0);
        }
    }
    free(stream_ctx);
}

quicrq_stream_ctx_t* quicrq_create_stream_context(quicrq_cnx_ctx_t* cnx_ctx, uint64_t stream_id)
{
    quicrq_stream_ctx_t* stream_ctx = (quicrq_stream_ctx_t*)malloc(sizeof(quicrq_stream_ctx_t));
    if (stream_ctx != NULL) {
        memset(stream_ctx, 0, sizeof(quicrq_stream_ctx_t));
        stream_ctx->cnx_ctx = cnx_ctx;
        stream_ctx->stream_id = stream_id;
        if (cnx_ctx->last_stream == NULL) {
            cnx_ctx->first_stream = stream_ctx;
        }
        else {
            cnx_ctx->last_stream->next_stream = stream_ctx;
        }
        stream_ctx->previous_stream = cnx_ctx->last_stream;
        cnx_ctx->last_stream = stream_ctx;
    }

    return stream_ctx;
}

quicrq_stream_ctx_t* quicrq_find_or_create_stream(
    uint64_t stream_id,
    quicrq_cnx_ctx_t* cnx_ctx,
    int should_create)
{
    quicrq_stream_ctx_t* stream_ctx = cnx_ctx->first_stream;

    while (stream_ctx != NULL) {
        if (stream_ctx->stream_id == stream_id) {
            break;
        }
        stream_ctx = stream_ctx->next_stream;
    }
    if (stream_ctx == NULL && should_create) {
        stream_ctx = quicrq_create_stream_context(cnx_ctx, stream_id);
    }

    return stream_ctx;
}

/* Media publisher API.
 * Simplified API for now:
 * - cnx_ctx: context of the QUICR connection
 * - media_url: URL of the media segment
 * - media_publisher_fn: callback function for processing media arrival
 * - media_ctx: media context managed by the publisher
 */


/* Subscribe to a media segment using QUIC streams.
 * Simplified API for now:
 * - cnx_ctx: context of the QUICR connection
 * - media_url: URL of the media segment
 * - 
 */
int quicrq_subscribe_media_stream(
    quicrq_cnx_ctx_t* cnx_ctx,
    char const* url,
    quicrq_media_consumer_fn media_consumer_fn,
    void* media_ctx)
{
    return -1;
}

/* Utility function, encode or decode a frame header.
 */
const uint8_t* quicr_decode_frame_header(const uint8_t* fh, const uint8_t* fh_max, quicrq_media_frame_header_t* hdr)
{
    /* decode the frame header */
    if ((fh = picoquic_frames_uint64_decode(fh, fh_max, &hdr->timestamp)) != NULL &&
        (fh = picoquic_frames_uint64_decode(fh, fh_max, &hdr->number)) != NULL){
        uint32_t length = 0;
        fh = picoquic_frames_uint32_decode(fh, fh_max, &length);
        hdr->length = length;
    }
    return fh;
}

uint8_t* quicr_encode_frame_header(uint8_t* fh, const uint8_t* fh_max, const quicrq_media_frame_header_t* hdr)
{
    /* decode the frame header */
    if ((fh = picoquic_frames_uint64_encode(fh, fh_max, hdr->timestamp)) != NULL &&
        (fh = picoquic_frames_uint64_encode(fh, fh_max, hdr->number)) != NULL) {
        fh = picoquic_frames_uint32_encode(fh, fh_max, (uint32_t)hdr->length);
    }

    return fh;
}
