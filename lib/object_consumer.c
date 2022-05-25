/* Implementation of the media object consumer API.
 * 
 * The application expects subscribes to receive a sequence of objects. 
 * This is implemented by a bridge between the old "fragment" API and
 * the object API. This requires a local context that keeps the required
 * state.
 */
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include "quicrq_internal.h"
#include "quicrq_reassembly.h"
#include "picoquic_utils.h"

typedef struct st_quicrq_object_consumer_bridge_ctx_t {
    quicrq_ctx_t* qr_ctx;
    quicrq_reassembly_context_t reassembly_ctx;
    quicrq_media_object_consumer_fn object_consumer_fn;
    void* media_object_ctx;
} quicrq_object_consumer_bridge_ctx_t;


/* Process fragments arriving to the bridge */
int quicrq_media_object_bridge_ready(
    void* media_ctx,
    uint64_t current_time,
    uint64_t object_id,
    const uint8_t* data,
    size_t data_length,
    quicrq_reassembly_object_mode_enum object_mode)
{
    int ret = 0;
    quicrq_object_consumer_bridge_ctx_t* bridge_ctx = (quicrq_object_consumer_bridge_ctx_t*)media_ctx;

    /* Find the object header */
    if (data_length < QUIRRQ_MEDIA_TEST_HEADER_SIZE) {
        /* Malformed object */
        ret = -1;
    }
    else {
        if (ret == 0) {
            /* TODO: for some streams, we may be able to "jump ahead" and
             * use the latest object without waiting for the full sequence */
            /* if in sequence, deliver the object to the application. */
            if (object_mode != quicrq_reassembly_object_peek) {
                /* Deliver to the application */
                ret = bridge_ctx->object_consumer_fn)(
                    quicrq_media_datagram_ready,
                    bridge_ctx->object_consumer_ctx,
                    current_time, object_id,
                    data data_length,  NULL);
            }
        }
    }
    return ret;
}

int quicrq_media_object_bridge_fn(
    quicrq_media_consumer_enum action,
    void* media_ctx,
    uint64_t current_time,
    const uint8_t* data,
    uint64_t object_id,
    uint64_t offset,
    uint64_t queue_delay,
    int is_last_fragment,
    size_t data_length)
{
    int ret = 0;
    quicrq_object_consumer_bridge_ctx_t* bridge_ctx = (quicrq_object_consumer_bridge_ctx_t*)media_ctx;

    switch (action) {
    case quicrq_media_datagram_ready:
        ret = quicrq_reassembly_input(&bridge_ctx->reassembly_ctx, current_time, data, object_id, offset, is_last_fragment, data_length,
            quicrq_media_object_bridge_ready, bridge_ctx);
        if (ret == 0 && cons_ctx->reassembly_ctx.is_finished) {
            ret = quicrq_consumer_finished;
        }
        break;
    case quicrq_media_final_object_id:
        ret = quicrq_reassembly_learn_final_object_id(&bridge_ctx->reassembly_ctx, final_object_id);
        if (ret == 0 && bridge_ctx->reassembly_ctx.is_finished) {
            ret = quicrq_consumer_finished;
        }
        break;
    case quicrq_media_close:
        quicrq_reassembly_release(&bridge_ctx->reassembly_ctx);
        free(media_ctx);
        break;
    default:
        ret = -1;
        break;
    }
    return ret;
}


/* Subscribe object stream. */
int quicrq_subscribe_object_stream(quicrq_ctx_t* qr_ctx, quicrq_cnx_ctx_t* cnx_ctx,
    const uint8_t* url, size_t url_length, int use_datagrams,
    quicrq_media_consumer_fn media_object_consumer_fn, void* media_object_ctx)
{
     quicrq_object_consumer_bridge_ctx_t* bridge_ctx = (quicrq_object_consumer_bridge_ctx_t*)malloc(sizeof(quicrq_object_consumer_bridge_ctx_t));
     if (bridge_ctx != NULL) {
         memset(bridge_ctx, 0, sizeof(quicrq_object_consumer_bridge_ctx_t));
         bridge_ctx->qr_ctx = qr_ctx;
         quicrq_reassembly_init(&cons_ctx->reassembly_ctx);
         /* Create a media context for the stream */
         ret = quicrq_cnx_subscribe_media(cnx_ctx, url, url_length, use_datagrams,
             quicrq_media_object_bridge_fn, bridge_ctx);
         if (ret != 0) {
             free(bridge_ctx);
             bridge_ctx = NULL;
         }
     }

     return bridge_ctx;
}