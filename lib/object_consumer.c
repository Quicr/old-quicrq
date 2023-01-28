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
#include <picoquic.h>
#include "quicrq.h"
#include "quicrq_internal.h"
#include "quicrq_reassembly.h"
#include "picoquic_utils.h"

typedef struct st_quicrq_object_stream_consumer_ctx {
    quicrq_ctx_t* qr_ctx;
    quicrq_stream_ctx_t* stream_ctx;
    quicrq_reassembly_context_t reassembly_ctx;
    quicrq_object_stream_consumer_fn object_stream_consumer_fn;
    void * object_stream_consumer_ctx;
    int in_order_required;
} quicrq_object_stream_consumer_ctx;


/* Process fragments arriving to the bridge */
int quicrq_media_object_bridge_ready(
    void* media_ctx,
    uint64_t current_time,
    uint64_t group_id,
    uint64_t object_id,
    uint8_t flags,
    const uint8_t* data,
    size_t data_length,
    quicrq_reassembly_object_mode_enum object_mode)
{
    int ret = 0;
    quicrq_object_stream_consumer_ctx* bridge_ctx = (quicrq_object_stream_consumer_ctx*)media_ctx;

    /* TODO: for some streams, we may be able to "jump ahead" and
        * use the latest object without waiting for the full sequence */
    /* if in sequence, deliver the object to the application. */
    if ((bridge_ctx->in_order_required && object_mode != quicrq_reassembly_object_peek) ||
        (!bridge_ctx->in_order_required && object_mode != quicrq_reassembly_object_repair)){
#if 1
        if (group_id == 0) {
            DBG_PRINTF("%s", "Bug");
        }
#endif
        /* Deliver to the application */
        quicrq_object_stream_consumer_properties_t properties = { 0 };
        properties.flags = flags;
        ret = bridge_ctx->object_stream_consumer_fn(
            quicrq_media_datagram_ready,
            bridge_ctx->object_stream_consumer_ctx,
            current_time, group_id, object_id,
            data, data_length,  &properties, 0, 0);
    }
       
    return ret;
}

int quicrq_media_object_bridge_fn(
    quicrq_media_consumer_enum action,
    void* media_ctx,
    uint64_t current_time,
    const uint8_t* data,
    uint64_t group_id,
    uint64_t object_id,
    uint64_t offset,
    uint64_t queue_delay,
    uint8_t flags,
    uint64_t nb_objects_previous_group,
    int is_last_fragment,
    size_t data_length)
{
    int ret = 0;
    quicrq_object_stream_consumer_ctx* bridge_ctx = (quicrq_object_stream_consumer_ctx*)media_ctx;

    switch (action) {
    case quicrq_media_datagram_ready:
        ret = quicrq_reassembly_input(&bridge_ctx->reassembly_ctx, current_time, data, group_id, object_id, offset, 
            queue_delay, flags,
            nb_objects_previous_group, is_last_fragment, data_length,
            quicrq_media_object_bridge_ready, bridge_ctx);
        if (ret == 0 && bridge_ctx->reassembly_ctx.is_finished) {
            ret = quicrq_consumer_finished;
        }
        break;
    case quicrq_media_final_object_id:
        ret = quicrq_reassembly_learn_final_object_id(&bridge_ctx->reassembly_ctx, group_id, object_id);
        if (ret == 0 && bridge_ctx->reassembly_ctx.is_finished) {
            ret = quicrq_consumer_finished;
        }
        break;
    case quicrq_media_real_time_cache:
        /* Nothing to do there. */
        break;
    case quicrq_media_start_point:
        ret = quicrq_reassembly_learn_start_point(&bridge_ctx->reassembly_ctx, group_id, object_id, current_time,
            quicrq_media_object_bridge_ready, bridge_ctx);
        if (ret == 0 && bridge_ctx->reassembly_ctx.is_finished) {
            ret = quicrq_consumer_finished;
        }
        break;
    case quicrq_media_close:
        ret = bridge_ctx->object_stream_consumer_fn(
            quicrq_media_close,
            bridge_ctx->object_stream_consumer_ctx,
            current_time, group_id, object_id,
            NULL, 0, NULL, 0, 0);
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
quicrq_object_stream_consumer_ctx* quicrq_subscribe_object_stream(quicrq_cnx_ctx_t* cnx_ctx,
    const uint8_t* url, size_t url_length, quicrq_transport_mode_enum transport_mode,
    int in_order_required, quicrq_subscribe_intent_t * intent,
    quicrq_object_stream_consumer_fn object_stream_consumer_fn, void* object_stream_consumer_ctx)
{
    quicrq_object_stream_consumer_ctx* bridge_ctx = (quicrq_object_stream_consumer_ctx*)malloc(sizeof(quicrq_object_stream_consumer_ctx));
    if (bridge_ctx != NULL) {
        int ret;

        memset(bridge_ctx, 0, sizeof(quicrq_object_stream_consumer_ctx));
        bridge_ctx->qr_ctx = cnx_ctx->qr_ctx;
        bridge_ctx->object_stream_consumer_fn = object_stream_consumer_fn;
        bridge_ctx->object_stream_consumer_ctx = object_stream_consumer_ctx;
        bridge_ctx->in_order_required = in_order_required;
        quicrq_reassembly_init(&bridge_ctx->reassembly_ctx);
        /* Create a media context for the stream */
        ret = quicrq_cnx_subscribe_media_ex(cnx_ctx, url, url_length, transport_mode, intent,
            quicrq_media_object_bridge_fn, bridge_ctx, &bridge_ctx->stream_ctx);
        if (ret != 0) {
            free(bridge_ctx);
            bridge_ctx = NULL;
        }
    }

     return bridge_ctx;
}

void quicrq_unsubscribe_object_stream(quicrq_object_stream_consumer_ctx* bridge_ctx)
{

    if (bridge_ctx->stream_ctx->close_reason == quicrq_media_close_reason_unknown) {
        bridge_ctx->stream_ctx->close_reason = quicrq_media_close_local_application;
    }
    quicrq_delete_stream_ctx(bridge_ctx->stream_ctx->cnx_ctx, bridge_ctx->stream_ctx);
    bridge_ctx->object_stream_consumer_fn = NULL;
    bridge_ctx->object_stream_consumer_ctx = NULL;
}
