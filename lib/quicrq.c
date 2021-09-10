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

#include "quicrq.h"
#include "quicrq_internal.h"

/* New request: media segment.
 * Create a connection to the upstream server.
 * Queue a media request message.
 */

/* Incoming request: receive media segment.
 * Check whether the segment is available, etc.
 */

/* Poll for data on a stream. Either send pending request (upstream)
 * or push available media (downstream)
 */

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
#if 0
            /* TODO: this depends on role, stream, etc.*/
            else if (stream_ctx->is_name_read) {
                /* Write after fin? */
                return(-1);
            }
            else {
                /* Accumulate data */
                size_t available = sizeof(stream_ctx->file_name) - stream_ctx->name_length - 1;

                if (length > available) {
                    /* Name too long: reset stream! */
                    sample_server_delete_stream_context(cnx_ctx, stream_ctx);
                    (void)picoquic_reset_stream(cnx, stream_id, PICOQUIC_SAMPLE_NAME_TOO_LONG_ERROR);
                }
                else {
                    if (length > 0) {
                        memcpy(stream_ctx->file_name + stream_ctx->name_length, bytes, length);
                        stream_ctx->name_length += length;
                    }
                    if (fin_or_event == picoquic_callback_stream_fin) {
                        int stream_ret;

                        /* If fin, mark read, check the file, open it. Or reset if there is no such file */
                        stream_ctx->file_name[stream_ctx->name_length + 1] = 0;
                        stream_ctx->is_name_read = 1;
                        stream_ret = sample_server_open_stream(cnx_ctx, stream_ctx);

                        if (stream_ret == 0) {
                            /* If data needs to be sent, set the context as active */
                            ret = picoquic_mark_active_stream(cnx, stream_id, 1, stream_ctx);
                        }
                        else {
                            /* If the file could not be read, reset the stream */
                            sample_server_delete_stream_context(cnx_ctx, stream_ctx);
                            (void)picoquic_reset_stream(cnx, stream_id, stream_ret);
                        }
                    }
                }
            }
#endif
            break;
        case picoquic_callback_prepare_to_send:
            /* Active sending API */
            if (stream_ctx == NULL) {
                /* This should never happen */
            }
#if 0
            /* TODO: this depends on stream, role, etc. */
            else if (stream_ctx->F == NULL) {
                /* Error, asking for data after end of file */
            }
            else {
                /* Implement the zero copy callback */
                size_t available = stream_ctx->file_length - stream_ctx->file_sent;
                int is_fin = 1;
                uint8_t* buffer;

                if (available > length) {
                    available = length;
                    is_fin = 0;
                }

                buffer = picoquic_provide_stream_data_buffer(bytes, available, is_fin, !is_fin);
                if (buffer != NULL) {
                    size_t nb_read = fread(buffer, 1, available, stream_ctx->F);

                    if (nb_read != available) {
                        /* Error while reading the file */
                        sample_server_delete_stream_context(cnx_ctx, stream_ctx);
                        (void)picoquic_reset_stream(cnx, stream_id, PICOQUIC_SAMPLE_FILE_READ_ERROR);
                    }
                    else {
                        stream_ctx->file_sent += available;
                    }
                }
                else {
                    /* Should never happen according to callback spec. */
                    ret = -1;
                }
            }
#endif
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
            quicrq_callback, &qr_ctx, NULL, NULL, NULL, current_time, p_simulated_time,
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
    /* Delete the stream contexts */
    while (cnx_ctx->first_stream != NULL) {
        quicrq_delete_stream_ctx(cnx_ctx, cnx_ctx->first_stream);
    }
    /* Remove the connection from the double linked list */
    if (cnx_ctx->quicrq_ctx != NULL) {
        if (cnx_ctx->next_cnx == NULL) {
            cnx_ctx->quicrq_ctx->last_cnx = cnx_ctx->previous_cnx;
        }
        else {
            cnx_ctx->next_cnx->previous_cnx = cnx_ctx->previous_cnx;
        }
        if (cnx_ctx->previous_cnx == NULL) {
            cnx_ctx->quicrq_ctx->first_cnx = cnx_ctx->next_cnx;
        }
        else {
            cnx_ctx->previous_cnx->next_cnx = cnx_ctx->next_cnx;
        }
    }
    /* Delete the quic connection */
    if (cnx_ctx->cnx != NULL) {
        picoquic_delete_cnx(cnx_ctx->cnx);
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

    free(stream_ctx);
}

quicrq_stream_ctx_t* quicrq_create_stream_context(quicrq_cnx_ctx_t* cnx_ctx, uint64_t stream_id)
{
    quicrq_stream_ctx_t* stream_ctx = (quicrq_stream_ctx_t*)malloc(sizeof(quicrq_stream_ctx_t));
    if (stream_ctx != NULL) {
        memset(stream_ctx, 0, sizeof(quicrq_stream_ctx_t));
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