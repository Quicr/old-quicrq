

/* Implementation of the media object source API
 */
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include "quicrq_internal.h"
#include "quicrq_fragment.h"
#include "picoquic_utils.h"

/* Object Source API functions.
 */

quicrq_media_object_source_ctx_t* quicrq_publish_object_source(quicrq_ctx_t* qr_ctx, const uint8_t* url, size_t url_length,
    quicrq_media_object_source_properties_t* properties)
{
    /* Create the media object source context, and register it in the quicrq context */
    quicrq_media_object_source_ctx_t* object_source_ctx = (quicrq_media_object_source_ctx_t*)
        malloc(sizeof(quicrq_media_object_source_ctx_t));
    if (object_source_ctx != NULL) {
        int ret = 0;

        memset(object_source_ctx, 0, sizeof(quicrq_media_object_source_ctx_t));
        object_source_ctx->qr_ctx = qr_ctx;
        /* Add to double linked list of sources for context */
        if (qr_ctx->last_object_source == NULL) {
            qr_ctx->first_object_source = object_source_ctx;
            qr_ctx->last_object_source = object_source_ctx;
        }
        else {
            qr_ctx->last_object_source->next_in_qr_ctx = object_source_ctx;
            object_source_ctx->previous_in_qr_ctx = qr_ctx->last_object_source;
            qr_ctx->last_object_source = object_source_ctx;
        }
        /* Copy the properties */
        if (properties != NULL) {
            memcpy(&object_source_ctx->properties, properties, sizeof(quicrq_media_object_source_properties_t));
        }
        /* create and initialize fragment cache, publish the corresponding source,
        * then publish the corresponding source.
        */
        object_source_ctx->cache_ctx = quicrq_fragment_cache_create_ctx(qr_ctx);
        if (object_source_ctx->cache_ctx == NULL) {
            ret = -1;
        } else {
            /* create  qucirq_srce_media_ctx and set it on the cache_ctx */
            ret = quicrq_publish_fragment_cached_media(qr_ctx, object_source_ctx->cache_ctx, url, url_length, 1, object_source_ctx->properties.use_real_time_caching);
            /* If needs be, set the media start point. */
            if (ret == 0 && (object_source_ctx->properties.start_group_id != 0 || object_source_ctx->properties.start_object_id != 0)) {
                ret = quicrq_fragment_cache_learn_start_point(object_source_ctx->cache_ctx, object_source_ctx->properties.start_group_id,
                    object_source_ctx->properties.start_object_id);
                if (ret == 0) {
                    if (object_source_ctx->next_group_id < object_source_ctx->properties.start_group_id ||
                        (object_source_ctx->next_group_id == object_source_ctx->properties.start_group_id &&
                            object_source_ctx->next_object_id < object_source_ctx->properties.start_object_id)) {
                        object_source_ctx->next_group_id = object_source_ctx->properties.start_group_id;
                        object_source_ctx->next_object_id = object_source_ctx->properties.start_object_id;
                    }
                }
            }
        }
        /* If the API fails, close the media object source */
        if (ret != 0) {
            DBG_PRINTF("%s", "Could not publish media source for media object source");
            quicrq_delete_object_source(object_source_ctx);
            object_source_ctx = NULL;
        }
    }
    return(object_source_ctx);
}

int quicrq_publish_object(
    quicrq_media_object_source_ctx_t* object_source_ctx,
    uint8_t* object_data,
    size_t object_length,
    quicrq_media_object_properties_t* properties,
    uint64_t group_id,
    uint64_t object_id)
{
    int ret = 0;
    uint64_t current_time = picoquic_get_quic_time(object_source_ctx->qr_ctx->quic);
    uint64_t nb_objects_previous_group = 0;
    int is_new_group = 0;

    /* Verify that the progression of numbers by the application matches the rules */
    if (group_id != object_source_ctx->next_group_id) {
        if (group_id != object_source_ctx->next_group_id + 1 ||
            object_id != 0 || object_source_ctx->next_object_id == 0) {
            ret = -1;
        }
        else {
            is_new_group = 1;
            nb_objects_previous_group = object_source_ctx->next_object_id;
            object_source_ctx->next_group_id++;
            object_source_ctx->next_object_id = 0;
        }
    }
    else if (object_id !=  object_source_ctx->next_object_id){
        ret = -1;
    }

    if (ret == 0) {
        ret = quicrq_fragment_propose_to_cache(object_source_ctx->cache_ctx,
            object_data, object_source_ctx->next_group_id, object_source_ctx->next_object_id,
            /* offset */ 0, /* queue delay */ 0, properties->flags, nb_objects_previous_group,
            /* is_last_fragment */ 1, object_length, current_time);
        if (ret == 0) {
            object_source_ctx->next_object_id++;
        }
    }
    return ret;
}

void quicrq_publish_object_fin(quicrq_media_object_source_ctx_t* object_source_ctx)
{
    /* Document the final group-ID and object-ID in context */
    (void) quicrq_fragment_cache_learn_end_point(object_source_ctx->cache_ctx,
        object_source_ctx->next_group_id, object_source_ctx->next_object_id);
}

void quicrq_delete_object_source(quicrq_media_object_source_ctx_t* object_source_ctx)
{
    if (object_source_ctx->cache_ctx != NULL) {
        /* Close the corresponding source context */
        if (object_source_ctx->cache_ctx->srce_ctx != NULL) {
            quicrq_delete_source(object_source_ctx->cache_ctx->srce_ctx, object_source_ctx->qr_ctx);
        }
        else {
            /* Explicitly delete cache in rare cases where not yet connected to fragment source */
            quicrq_fragment_cache_delete_ctx(object_source_ctx->cache_ctx);
        }
        object_source_ctx->cache_ctx = NULL;
    }
    /* Unlink from Quicr context */
    if (object_source_ctx->qr_ctx->first_object_source == object_source_ctx) {
        object_source_ctx->qr_ctx->first_object_source = object_source_ctx->next_in_qr_ctx;
    }
    else if (object_source_ctx->previous_in_qr_ctx != NULL) {
        object_source_ctx->previous_in_qr_ctx->next_in_qr_ctx = object_source_ctx->next_in_qr_ctx;
    }
    if (object_source_ctx->qr_ctx->last_object_source == object_source_ctx) {
        object_source_ctx->qr_ctx->last_object_source = object_source_ctx->previous_in_qr_ctx;
    }
    else if (object_source_ctx->next_in_qr_ctx != NULL) {
        object_source_ctx->next_in_qr_ctx->previous_in_qr_ctx = object_source_ctx->previous_in_qr_ctx;
    }
    /* Free the resource */
    free(object_source_ctx);
}
