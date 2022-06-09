

/* Implementation of the media object source API
 */
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include "quicrq_internal.h"
#include "picoquic_utils.h"

/* Data management functions.
 * The cache of objects is managed as a splay, allowing direct access
 * to individual objects. Cache management will keep the size of
 * the splay reasonable.
 * For simplicity, the object data is allocated just after the
 * quicrq_object_source_object_t structure.
 */
typedef struct st_quicrq_object_source_item_t {
    picosplay_node_t object_node;
    uint64_t object_id;
    uint64_t object_time;
    size_t object_length;
    uint8_t* object;
    quicrq_media_object_properties_t properties;
} quicrq_object_source_item_t;


static void* quicrq_object_source_node_value(picosplay_node_t* object_node)
{
    return (object_node == NULL) ? NULL : (void*)((char*)object_node - offsetof(struct st_quicrq_object_source_item_t, object_node));
}

static int64_t quicrq_object_source_node_compare(void* l, void* r) {
    return (int64_t)((quicrq_object_source_item_t*)l)->object_id - ((quicrq_object_source_item_t*)r)->object_id;
}

static picosplay_node_t* quicrq_object_source_node_create(void* v_media_object)
{
    return &((quicrq_object_source_item_t*)v_media_object)->object_node;
}

static void quicrq_object_source_node_delete(void* tree, picosplay_node_t* object_node)
{
#ifdef _WINDOWS
    UNREFERENCED_PARAMETER(tree);
#endif
    free(quicrq_object_source_node_value(object_node));
}

void quicrq_object_source_list_init(quicrq_media_object_source_ctx_t* object_source_ctx)
{
    picosplay_init_tree(&object_source_ctx->object_source_tree, quicrq_object_source_node_compare,
        quicrq_object_source_node_create, quicrq_object_source_node_delete, quicrq_object_source_node_value);
}

quicrq_object_source_item_t* quicrq_get_object_source_item(quicrq_media_object_source_ctx_t* object_source_ctx, uint64_t object_id)
{
    quicrq_object_source_item_t key = { 0 };
    key.object_id = object_id;
    quicrq_object_source_item_t* found = (quicrq_object_source_item_t*)quicrq_object_source_node_value(
        picosplay_find(&object_source_ctx->object_source_tree, &key));
    return found;
}

/* Publisher functions.
 */
typedef struct st_quicrq_object_source_publisher_ctx_t {
    quicrq_media_object_source_ctx_t* object_source_ctx;
    uint64_t next_object_id;
    size_t next_object_offset;
    int next_was_sent;
} quicrq_object_source_publisher_ctx_t;

void* quicrq_media_object_publisher_subscribe(void* pub_ctx, quicrq_stream_ctx_t * stream_ctx)
{
    quicrq_media_object_source_ctx_t* object_srce_ctx = (quicrq_media_object_source_ctx_t*)pub_ctx;
    quicrq_object_source_publisher_ctx_t* media_ctx = (quicrq_object_source_publisher_ctx_t*)
        malloc(sizeof(quicrq_object_source_publisher_ctx_t));
    if (media_ctx != NULL) {
        memset(media_ctx, 0, sizeof(quicrq_object_source_publisher_ctx_t));
        media_ctx->object_source_ctx = object_srce_ctx;
        media_ctx->next_object_id = object_srce_ctx->start_object_id;
        if (stream_ctx != NULL) {
            stream_ctx->start_object_id = object_srce_ctx->start_object_id;
            stream_ctx->next_object_id = object_srce_ctx->start_object_id;
        }
        /* TODO: additional properties */
    }
    return media_ctx;
}


void quicrq_media_object_publisher_delete(void* pub_ctx)
{
    free(pub_ctx);
}

int quicrq_media_object_publisher(
    quicrq_media_source_action_enum action,
    void* v_media_ctx,
    uint8_t* data,
    size_t data_max_size,
    size_t* data_length,
    int* is_last_fragment,
    int* is_media_finished,
    int* is_still_active,
    uint64_t current_time)
{
    int ret = 0;
    quicrq_object_source_publisher_ctx_t* media_ctx = (quicrq_object_source_publisher_ctx_t*)v_media_ctx;
    quicrq_object_source_item_t* object_source_item = NULL;
    if (action == quicrq_media_source_get_data) {
        *is_media_finished = 0;
        *is_last_fragment = 0;
        *is_still_active = 0;
        *data_length = 0;
        /* if the current object ID is already published, find the next one */
        if (media_ctx->next_was_sent) {
            media_ctx->next_object_id++;
            media_ctx->next_object_offset = 0;
            media_ctx->next_was_sent = 0;
        }
        /* retrieve the object */
        object_source_item = quicrq_get_object_source_item(media_ctx->object_source_ctx, media_ctx->next_object_id);
        if (object_source_item == NULL) {
            /* This object is not yet available */
            if (media_ctx->object_source_ctx->is_finished) {
                *is_media_finished = 1;
            }
        } else {
            size_t available = object_source_item->object_length - media_ctx->next_object_offset;
            size_t copied = data_max_size;

            *is_still_active = 1;
            if (data_max_size >= available) {
                *is_last_fragment = 1;
                copied = available;
            }
            *data_length = copied;
            if (data != NULL) {
                /* If data is set to NULL, return the available size but do not copy anything */
                memcpy(data, object_source_item->object + media_ctx->next_object_offset, copied);
                media_ctx->next_object_offset += copied;
                if (media_ctx->next_object_offset >= object_source_item->object_length) {
                    media_ctx->next_was_sent = 1;
                }
            }
        }
    }
    else if (action == quicrq_media_source_close) {
        /* close the context */
        quicrq_media_object_publisher_delete(media_ctx);
    }
    return ret;
}

/* Object Source API functions.
 */

static void quicrq_unlink_object_source_publisher(void* v_object_source_ctx)
{
    quicrq_media_object_source_ctx_t* object_source_ctx = (quicrq_media_object_source_ctx_t*)v_object_source_ctx;
    object_source_ctx->media_source_ctx = NULL;
}

quicrq_media_object_source_ctx_t* quicrq_publish_object_source(quicrq_ctx_t* qr_ctx, const uint8_t* url, size_t url_length,
    quicrq_media_object_source_properties_t* properties)
{
    /* Create the media object source context, and register it in the quicrq context */
    quicrq_media_object_source_ctx_t* object_source_ctx = (quicrq_media_object_source_ctx_t*)
        malloc(sizeof(quicrq_media_object_source_ctx_t));
    if (object_source_ctx != NULL) {
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
        /* Initialize the list of objects */
        quicrq_object_source_list_init(object_source_ctx);
        /* Call the publisher API */
        object_source_ctx->media_source_ctx = quicrq_publish_source(qr_ctx, url, url_length,
            (void*)object_source_ctx, quicrq_media_object_publisher_subscribe,
            quicrq_media_object_publisher, quicrq_unlink_object_source_publisher);
        /* If the API fails, close the media object source */
        if (object_source_ctx->media_source_ctx == NULL) {
            DBG_PRINTF("%s", "Could not publish media source for media object source");
            quicrq_delete_object_source(object_source_ctx);
            object_source_ctx = NULL;
        }
    }
    return(object_source_ctx);
}

int quicrq_object_source_set_start(quicrq_media_object_source_ctx_t* object_source_ctx, uint64_t start_group_id, uint64_t start_object_id)
{
    int ret = 0;

    if (object_source_ctx->start_group_id == 0 && object_source_ctx->start_object_id == 0 && object_source_ctx->next_object_id == 0) {
        object_source_ctx->start_group_id = start_group_id;
        object_source_ctx->start_object_id = start_object_id;
        object_source_ctx->next_object_id = start_object_id;
    }
    else {
        ret = -1;
    }
    return ret;
}

int quicrq_publish_object(
    quicrq_media_object_source_ctx_t* object_source_ctx,
    uint8_t* object_data,
    size_t object_length,
    quicrq_media_object_properties_t* properties)
{
    int ret = 0;
    size_t allocated = sizeof(quicrq_object_source_item_t) + object_length;
    quicrq_object_source_item_t* source_object = (quicrq_object_source_item_t*) malloc(allocated);
    if (source_object == NULL) {
        ret = -1;
    }
    else {
        /* Add the object at the end of the cache. */
        memset(source_object, 0, allocated);
        source_object->object_id = object_source_ctx->next_object_id;
        object_source_ctx->next_object_id++;
        source_object->object_length = object_length;
        source_object->object_time = picoquic_get_quic_time(object_source_ctx->qr_ctx->quic);
        source_object->object = ((uint8_t *)source_object) + sizeof(quicrq_object_source_item_t);
        memcpy(source_object->object, object_data, object_length);
        (void)picosplay_insert(&object_source_ctx->object_source_tree, source_object);
        /* Signal to the quic context that the source is now active. */
        if (object_source_ctx->media_source_ctx != NULL) {
            quicrq_source_wakeup(object_source_ctx->media_source_ctx);
        }
    }
    return ret;
}

void quicrq_publish_object_fin(quicrq_media_object_source_ctx_t* object_source_ctx)
{
    object_source_ctx->is_finished = 1;
    quicrq_source_wakeup(object_source_ctx->media_source_ctx);
}

void quicrq_delete_object_source(quicrq_media_object_source_ctx_t* object_source_ctx)
{
    /* Close the corresponding source context */
    if (object_source_ctx->media_source_ctx != NULL) {
        quicrq_media_source_ctx_t* media_ctx = object_source_ctx->media_source_ctx;
        object_source_ctx->media_source_ctx = NULL;
        quicrq_delete_source(media_ctx, object_source_ctx->qr_ctx);
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