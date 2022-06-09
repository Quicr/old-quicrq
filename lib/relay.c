/* Handling of a relay */
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include "picoquic_utils.h"
#include "picosplay.h"
#include "quicrq.h"
#include "quicrq_reassembly.h"
#include "quicrq_internal.h"
#include "quicrq_relay_internal.h"

/* A relay is a specialized node, acting both as client when acquiring a media
 * fragment and as server when producing data.
 * 
 * There is one QUICRQ context per relay, used both for initiating a connection to
 * the server, and accepting connections from the client.
 * 
 * When a client requests an URL from the relay, the relay checks whether that URL is
 * already published, i.e., present in the local cache. If it is, then the client is
 * connected to that source. If not, the source is created and a request to the
 * server is started, in order to acquire the URL.
 * 
 * When  a client posts an URL to the relay, the relay checks whether the URL exists
 * already. For now, we will treat that as an error case. If it does not, the
 * relay creates a context over which to receive the media, and POSTs the content to
 * the server.
 * 
 * The client half creates a list of media objects. For simplification, the server half will
 * only deal with the media objects that are fully received. When a media object is
 * fully received, it becomes available. We may consider a difference in
 * availability between "in-order" and "out-of-sequence" availablity, which
 * may need to be reflected in the contract between connection and sources.
 */

/* Manage the splay of cached fragments */
static void* quicrq_relay_cache_fragment_node_value(picosplay_node_t* fragment_node)
{
    return (fragment_node == NULL) ? NULL : (void*)((char*)fragment_node - offsetof(struct st_quicrq_relay_cached_fragment_t, fragment_node));
}

static int64_t quicrq_relay_cache_fragment_node_compare(void* l, void* r) {
    quicrq_relay_cached_fragment_t* ls = (quicrq_relay_cached_fragment_t*)l;
    quicrq_relay_cached_fragment_t* rs = (quicrq_relay_cached_fragment_t*)r;
    int64_t ret = ls->object_id - rs->object_id;
    if (ret == 0) {
        if (ls->offset < rs->offset) {
            ret = -1;
        }
        else if (ls->offset > rs->offset) {
            ret = 1;
        }
    }
    return ret;
}

static picosplay_node_t* quicrq_relay_cache_fragment_node_create(void* v_media_object)
{
    return &((quicrq_relay_cached_fragment_t*)v_media_object)->fragment_node;
}

static void quicrq_relay_cache_fragment_node_delete(void* tree, picosplay_node_t* node)
{
#ifdef _WINDOWS
    UNREFERENCED_PARAMETER(tree);
#endif
    quicrq_relay_cached_media_t* cached_media = (quicrq_relay_cached_media_t*)((char*)tree - offsetof(struct st_quicrq_relay_cached_media_t, fragment_tree));
    quicrq_relay_cached_fragment_t* fragment = (quicrq_relay_cached_fragment_t *)quicrq_relay_cache_fragment_node_value(node);

    if (fragment->previous_in_order == NULL) {
        cached_media->first_fragment = fragment->next_in_order;
    }
    else {
        fragment->previous_in_order->next_in_order = fragment->next_in_order;
    }

    if (fragment->next_in_order == NULL) {
        cached_media->last_fragment = fragment->previous_in_order;
    }
    else {
        fragment->next_in_order->previous_in_order = fragment->previous_in_order;
    }

    free(quicrq_relay_cache_fragment_node_value(node));
}

quicrq_relay_cached_fragment_t* quicrq_relay_cache_get_fragment(quicrq_relay_cached_media_t* cached_ctx, uint64_t object_id, uint64_t offset)
{
    quicrq_relay_cached_fragment_t key = { 0 };
    key.object_id = object_id;
    key.offset = offset;
    picosplay_node_t* fragment_node = picosplay_find(&cached_ctx->fragment_tree, &key);
    return (quicrq_relay_cached_fragment_t*)quicrq_relay_cache_fragment_node_value(fragment_node);
}

void quicrq_relay_cache_media_clear(quicrq_relay_cached_media_t* cached_media)
{
    cached_media->first_fragment = NULL;
    cached_media->last_fragment = NULL;
    picosplay_empty_tree(&cached_media->fragment_tree);
}

void quicrq_relay_cache_media_init(quicrq_relay_cached_media_t* cached_media)
{
    picosplay_init_tree(&cached_media->fragment_tree, quicrq_relay_cache_fragment_node_compare,
        quicrq_relay_cache_fragment_node_create, quicrq_relay_cache_fragment_node_delete,
        quicrq_relay_cache_fragment_node_value);
}


/* Client part of the relay.
 * The connection is started when a context is specialized to become a relay
 */
int quicrq_relay_add_fragment_to_cache(quicrq_relay_cached_media_t* cached_ctx,
    const uint8_t* data,
    uint64_t object_id,
    uint64_t offset,
    uint64_t queue_delay,
    int is_last_fragment,
    size_t data_length,
    uint64_t current_time)
{
    int ret = 0;
    quicrq_relay_cached_fragment_t* fragment = (quicrq_relay_cached_fragment_t*)malloc(
        sizeof(quicrq_relay_cached_fragment_t) + data_length);

    if (fragment == NULL) {
        ret = -1;
    }
    else {
        memset(fragment, 0, sizeof(quicrq_relay_cached_fragment_t));
        if (cached_ctx->last_fragment == NULL) {
            cached_ctx->first_fragment = fragment;
        }
        else {
            fragment->previous_in_order = cached_ctx->last_fragment;
            cached_ctx->last_fragment->next_in_order = fragment;
        }
        cached_ctx->last_fragment = fragment;
        fragment->object_id = object_id;
        fragment->offset = offset;
        fragment->cache_time = current_time;
        fragment->queue_delay = queue_delay;
        fragment->is_last_fragment = is_last_fragment;
        fragment->data = ((uint8_t*)fragment) + sizeof(quicrq_relay_cached_fragment_t);
        fragment->data_length = data_length;
        memcpy(fragment->data, data, data_length);
        picosplay_insert(&cached_ctx->fragment_tree, fragment);
    }

    return ret;
}

int quicrq_relay_propose_fragment_to_cache(quicrq_relay_cached_media_t* cached_ctx,
    const uint8_t* data,
    uint64_t object_id,
    uint64_t offset,
    uint64_t queue_delay,
    int is_last_fragment,
    size_t data_length,
    uint64_t current_time)
{
    int ret = 0;
    int data_was_added = 0;
    /* First check whether the object is in the cache. */
    /* If the object is in the cache, check whether this fragment is already received */
    quicrq_relay_cached_fragment_t * first_fragment_state = NULL;
    quicrq_relay_cached_fragment_t key = { 0 };

    if (object_id < cached_ctx->first_object_id) {
        /* This fragment is too old to be considered. */
        return 0;
    }

    key.object_id = object_id;
    key.offset = UINT64_MAX;
    picosplay_node_t* last_fragment_node = picosplay_find_previous(&cached_ctx->fragment_tree, &key);
    do {
        first_fragment_state = (quicrq_relay_cached_fragment_t*)quicrq_relay_cache_fragment_node_value(last_fragment_node);
        if (first_fragment_state == NULL || first_fragment_state->object_id != object_id ||
            first_fragment_state->offset + first_fragment_state->data_length < offset) {
            /* Insert the whole fragment */
            ret = quicrq_relay_add_fragment_to_cache(cached_ctx, data, object_id, offset, queue_delay, is_last_fragment, data_length, current_time);
            data_was_added = 1;
            /* Mark done */
            data_length = 0;
        }
        else
        {
            uint64_t previous_last_byte = first_fragment_state->offset + first_fragment_state->data_length;
            if (offset + data_length > previous_last_byte) {
                /* Some of the fragment data comes after this one. Submit */
                size_t added_length = offset + data_length - previous_last_byte;
                ret = quicrq_relay_add_fragment_to_cache(cached_ctx, data, object_id, offset, queue_delay, is_last_fragment, added_length, current_time);
                data_was_added = 1;
                data_length -= added_length;
            }
            if (offset >= first_fragment_state->offset) {
                /* What remained of the fragment overlaps with existing data */
                data_length = 0;
            }
            else {
                if (first_fragment_state->offset < offset + data_length) {
                    /* Some of the fragment data overlaps, remove it */
                    data_length = first_fragment_state->offset - offset;
                }
                last_fragment_node = picosplay_previous(last_fragment_node);
            }
        }
    } while (ret == 0 && data_length > 0);

    if (ret == 0 && data_was_added) {
        /* Wake up the consumers of this source */
        quicrq_source_wakeup(cached_ctx->srce_ctx);
        /* Check whether this object is now complete */
        last_fragment_node = picosplay_find_previous(&cached_ctx->fragment_tree, &key);
        first_fragment_state = (quicrq_relay_cached_fragment_t*)quicrq_relay_cache_fragment_node_value(last_fragment_node);
        if (first_fragment_state != NULL) {
            int last_is_final = first_fragment_state->is_last_fragment;
            uint64_t previous_offset = first_fragment_state->offset;

            while (last_is_final && previous_offset > 0) {
                last_fragment_node = picosplay_previous(last_fragment_node);
                if (last_fragment_node == NULL) {
                    last_is_final = 0;
                }
                else {
                    first_fragment_state = (quicrq_relay_cached_fragment_t*)quicrq_relay_cache_fragment_node_value(last_fragment_node);
                    if (first_fragment_state->object_id != object_id ||
                        first_fragment_state->offset + first_fragment_state->data_length < previous_offset) {
                        last_is_final = 0;
                    }
                    else {
                        previous_offset = first_fragment_state->offset;
                    }
                }
            }
            if (last_is_final) {
                /* The object was just completely received. Keep counts. */
                cached_ctx->nb_object_received += 1;
            }
        }
    }

    return ret;
}

int quicrq_relay_learn_start_point(quicrq_relay_cached_media_t* cached_ctx,
    uint64_t start_object_id)
{
    int ret = 0;
    /* Find all cache fragments that might be before the start point,
     * and delete them */
    picosplay_node_t* first_fragment_node = NULL;
    cached_ctx->first_group_id = 0;
    cached_ctx->first_object_id = start_object_id;
    while ((first_fragment_node = picosplay_first(&cached_ctx->fragment_tree)) != NULL) {
        quicrq_relay_cached_fragment_t* first_fragment_state = 
            (quicrq_relay_cached_fragment_t*)quicrq_relay_cache_fragment_node_value(first_fragment_node);
        if (first_fragment_state == NULL || first_fragment_state->object_id >= start_object_id) {
            break;
        }
        else {
            picosplay_delete_hint(&cached_ctx->fragment_tree, first_fragment_node);
        }
    }
    /* TODO: if the end is known, something special? */
    return ret;
}

/* Purging the old fragments from the cache.
 * There are two modes of operation.
 * In the general case, we want to make sure that all data has a chance of being
 * sent to the clients reading data from the cache. That means:
 *  - only delete objects if all previous objects have been already received,
 *  - only delete objects if all fragments have been received,
 *  - only delete objects if all fragments are old enough.
 * If the connection feeding the cache is closed, we will not get any new fragment,
 * so there is no point waiting for them to arrive.
 * 
 * Deleting cached entries updates the "first_object_id" visible in the cache.
 * If a client subscribes to the cached media after a cache update, that
 * client will see the object ID numbers from that new start point on.
 */

void quicrq_relay_cache_media_purge(
    quicrq_relay_cached_media_t* cached_media,
    uint64_t current_time,
    uint64_t cache_duration_max,
    uint64_t first_object_id_kept)
{
    picosplay_node_t* fragment_node;

    while ((fragment_node = picosplay_first(&cached_media->fragment_tree)) != NULL) {
        /* Locate the first fragment in object order */
        quicrq_relay_cached_fragment_t* fragment =
            (quicrq_relay_cached_fragment_t*)quicrq_relay_cache_fragment_node_value(fragment_node);
        /* Check whether that fragment should be kept */
        if (fragment->object_id >= first_object_id_kept || fragment->cache_time + cache_duration_max > current_time) {
            /* This fragment and all coming after it shall be kept. */
            break;
        }
        else {
            int should_delete = 1;
            if (!cached_media->is_closed) {
                picosplay_node_t* next_fragment_node = fragment_node;
                size_t next_offset = fragment->data_length;
                int last_found = fragment->is_last_fragment;
                should_delete = (fragment->object_id != cached_media->first_object_id) && fragment->offset == 0;

                while (should_delete && (next_fragment_node = picosplay_next(next_fragment_node)) != NULL) {
                    quicrq_relay_cached_fragment_t* next_fragment =
                        (quicrq_relay_cached_fragment_t*)quicrq_relay_cache_fragment_node_value(next_fragment_node);
                    if (next_fragment->object_id != fragment->object_id ||
                        next_fragment->cache_time + cache_duration_max > current_time ||
                        next_fragment->offset != next_offset) {
                        break;
                    }
                    else {
                        next_offset += next_fragment->data_length;
                        if (next_fragment->is_last_fragment) {
                            /* All fragments up to the last have been verified */
                            last_found = 1;
                            break;
                        }
                    }
                }
                should_delete &= last_found;
            }
            if (should_delete) {
                cached_media->first_object_id = fragment->object_id + 1;
                while ((fragment_node = picosplay_first(&cached_media->fragment_tree)) != NULL) {
                    quicrq_relay_cached_fragment_t* fragment =
                        (quicrq_relay_cached_fragment_t*)quicrq_relay_cache_fragment_node_value(fragment_node);
                    if (fragment->object_id >= cached_media->first_object_id) {
                        break;
                    }
                    else {
                        picosplay_delete_hint(&cached_media->fragment_tree, fragment_node);
                    }
                }
            }
        }
    }
}

int quicrq_relay_consumer_cb(
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
    quicrq_relay_consumer_context_t * cons_ctx = (quicrq_relay_consumer_context_t*)media_ctx;

    switch (action) {
    case quicrq_media_datagram_ready:
        /* Check that this datagram was not yet received.
         * This requires accessing the cache by object_id, offset and length. */
         /* Add fragment (or fragments) to cache */
        ret = quicrq_relay_propose_fragment_to_cache(cons_ctx->cached_ctx, data, object_id, offset, queue_delay, is_last_fragment, data_length, current_time);
        /* Manage fin of transmission */
        if (ret == 0) {
            /* If the final object id is known, and the number of fully received objects
             * matches that object id, then the transmission is finished. */
            if (cons_ctx->cached_ctx->final_object_id > 0 &&
                cons_ctx->cached_ctx->nb_object_received >= cons_ctx->cached_ctx->final_object_id) {
                ret = quicrq_consumer_finished;
            }
        }
        break;
    case quicrq_media_final_object_id:
        /* Document the final object-ID in context */
        cons_ctx->cached_ctx->final_object_id = object_id;
        /* Manage fin of transmission */
        if (cons_ctx->cached_ctx->nb_object_received >= cons_ctx->cached_ctx->final_object_id) {
            ret = quicrq_consumer_finished;
        }
        if (ret == 0) {
            /* wake up the clients waiting for data on this media */
            quicrq_source_wakeup(cons_ctx->cached_ctx->srce_ctx);
        }
        break;
    case quicrq_media_start_point:
        /* Document the start point, and clean the cache of data before that point */
        ret = quicrq_relay_learn_start_point(cons_ctx->cached_ctx, object_id);
        if (ret == 0) {
            /* Set the start point for the dependent streams. */
            quicrq_stream_ctx_t* stream_ctx = cons_ctx->cached_ctx->srce_ctx->first_stream;
            while (stream_ctx != NULL) {
                /* for each client waiting for data on this media,
                 * update the start point and then wakeup the stream 
                 * so the start point can be releayed. */
                stream_ctx->start_object_id = object_id;
                if (stream_ctx->cnx_ctx->cnx != NULL) {
                    picoquic_mark_active_stream(stream_ctx->cnx_ctx->cnx, stream_ctx->stream_id, 1, stream_ctx);
                }
                stream_ctx = stream_ctx->next_stream_for_source;
            }
        }
        break;
    case quicrq_media_close:
        /* Document the final object */
        if (cons_ctx->cached_ctx->final_object_id == 0) {
            cons_ctx->cached_ctx->final_object_id = cons_ctx->cached_ctx->nb_object_received;
        }
        cons_ctx->cached_ctx->is_closed = 1;
        /* Notify consumers of the stream */
        quicrq_source_wakeup(cons_ctx->cached_ctx->srce_ctx);
        /* Free the media context resource */
        free(media_ctx);
        break;

    default:
        ret = -1;
        break;
    }
    return ret;
}

/* Server part of the relay.
 * The publisher functions tested at client and server delivers data in sequence.
 * We can do that as a first approximation, but proper relay handling needs to consider
 * delivering data out of sequence too.
 * Theory of interaction:
 * - The client calls for "in sequence data"
 * - If there is some, proceed as usual.
 * - If there is a hole in the sequence, inform of the hole.
 * Upon notification of a hole, the client may either wait for the inline delivery,
 * so everything is sent in sequence, or accept out of sequence transmission.
 * If out of sequence transmission is accepted, the client starts polling
 * for the new object-id, offset zero.
 * When the correction is available, the client is notified, and polls for the
 * missing object-id.
 */
void quicrq_relay_delete_cache_ctx(quicrq_relay_cached_media_t* cache_ctx)
{
    quicrq_relay_cache_media_clear(cache_ctx);

    free(cache_ctx);
}

void quicrq_relay_publisher_close(quicrq_relay_publisher_context_t* media_ctx)
{
    free(media_ctx);
}

int quicrq_relay_publisher_fn(
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

    quicrq_relay_publisher_context_t* media_ctx = (quicrq_relay_publisher_context_t*)v_media_ctx;
    if (action == quicrq_media_source_get_data) {
        *is_media_finished = 0;
        *is_last_fragment = 0;
        *is_still_active = 0;
        *data_length = 0;
        /* In sequence access to objects
         * variable current_object_id = in sequence.
         * variable current_offset = current_offset sent.
         */
        if (media_ctx->cache_ctx->final_object_id != 0 && media_ctx->current_object_id >= media_ctx->cache_ctx->final_object_id) {
            *is_media_finished = 1;
        }
        else {
            if (media_ctx->current_fragment == NULL) {
                /* Find the fragment with the expected offset */
                media_ctx->current_fragment = quicrq_relay_cache_get_fragment(media_ctx->cache_ctx, media_ctx->current_object_id, media_ctx->current_offset);
            }
            if (media_ctx->current_fragment == NULL) {
                /* Check for end of media maybe */
            }
            else {
                size_t available = media_ctx->current_fragment->data_length - media_ctx->length_sent;
                size_t copied = data_max_size;
                int end_of_fragment = 0;

                if (data_max_size >= available) {
                    end_of_fragment = 1;
                    *is_last_fragment = media_ctx->current_fragment->is_last_fragment;
                    copied = available;
                }
                *data_length = copied;
                *is_still_active = 1;
                if (data != NULL) {
                    /* If data is set to NULL, return the available size but do not copy anything */
                    memcpy(data, media_ctx->current_fragment->data + media_ctx->length_sent, copied);
                    media_ctx->length_sent += copied;
                    if (end_of_fragment) {
                        if (media_ctx->current_fragment->is_last_fragment) {
                            media_ctx->current_object_id++;
                            media_ctx->current_offset = 0;
                        }
                        else {
                            media_ctx->current_offset += media_ctx->current_fragment->data_length;
                        }

                        media_ctx->length_sent = 0;
                        media_ctx->current_fragment = NULL;
                    }
                }
            }
        }
    }
    else if (action == quicrq_media_source_close) {
        /* close the context */
        quicrq_relay_publisher_close(media_ctx);
    }
    return ret;
}

int quicrq_relay_datagram_publisher_prepare(
    quicrq_stream_ctx_t* stream_ctx,
    quicrq_relay_publisher_context_t* media_ctx,
    uint64_t datagram_stream_id,
    void* context,
    size_t space,
    int* media_was_sent,
    int* at_least_one_active,
    int * not_ready)
{
    int ret = 0;

    *media_was_sent = 0;
    *not_ready = 0;

    /* Check whether the current fragment is fully sent, progress if needed */
    if (media_ctx->current_fragment == NULL) {
        media_ctx->current_fragment = media_ctx->cache_ctx->first_fragment;
    }

    /* Move to the next fragment if it is available */
    while (media_ctx->current_fragment != NULL && media_ctx->length_sent >= media_ctx->current_fragment->data_length &&
        media_ctx->current_fragment->next_in_order != NULL) {
        media_ctx->length_sent = 0;
        media_ctx->current_fragment = media_ctx->current_fragment->next_in_order;
    }

    /* Return the flags per fragment */
    if (media_ctx->current_fragment != NULL && media_ctx->length_sent < media_ctx->current_fragment->data_length) {
        /* TODO: how to assess that the media is finished? */
        size_t offset = media_ctx->current_fragment->offset + media_ctx->length_sent;
        uint8_t datagram_header[QUICRQ_DATAGRAM_HEADER_MAX];
        uint8_t* h_byte = quicrq_datagram_header_encode(datagram_header, datagram_header + QUICRQ_DATAGRAM_HEADER_MAX,
            datagram_stream_id, media_ctx->current_fragment->group_id, media_ctx->current_fragment->object_id, offset,
            media_ctx->current_fragment->queue_delay, media_ctx->current_fragment->flags, 0);
        if (h_byte == NULL) {
            ret = -1;
        }
        else {
            int is_last_fragment = 0;
            size_t h_size = h_byte - datagram_header;

            if (h_size > space) {
                /* TODO: should get a min encoding length per stream */
                /* Can't do anything there */
                *at_least_one_active = 1;
            }
            else {
                size_t available = media_ctx->current_fragment->data_length - media_ctx->length_sent;
                size_t copied = space - h_size;
                if (copied >= available) {
                    is_last_fragment = media_ctx->current_fragment->is_last_fragment;
                    copied = available;
                }
                if (copied > 0) {
                    /* Get a buffer inside the datagram packet */
                    void* buffer = picoquic_provide_datagram_buffer(context, copied + h_size);
                    if (buffer == NULL) {
                        ret = -1;
                    }
                    else {
                        /* Push the header */
                        if (is_last_fragment) {
                            h_byte = quicrq_datagram_header_encode(datagram_header, datagram_header + QUICRQ_DATAGRAM_HEADER_MAX,
                                datagram_stream_id, media_ctx->current_fragment->group_id, media_ctx->current_fragment->object_id, offset,
                                media_ctx->current_fragment->queue_delay, media_ctx->current_fragment->flags, 1);

                            if (h_byte != datagram_header + h_size) {
                                /* Can't happen, unless our coding assumptions were wrong. Need to debug that. */
                                ret = -1;
                            }
                        }
                        if (ret == 0) {
                            memcpy(buffer, datagram_header, h_size);
                            /* Get the media */
                            memcpy(((uint8_t*)buffer) + h_size, media_ctx->current_fragment->data + media_ctx->length_sent, copied);
                            media_ctx->length_sent += copied;
                            *media_was_sent = 1;
                            *at_least_one_active = 1;
                            if (stream_ctx != NULL) {
                                /* Keep track in stream context */
                                ret = quicrq_datagram_ack_init(stream_ctx, media_ctx->current_fragment->object_id, offset, 
                                    ((uint8_t*)buffer) + h_size, copied,
                                    media_ctx->current_fragment->queue_delay, is_last_fragment, NULL, 
                                    picoquic_get_quic_time(stream_ctx->cnx_ctx->qr_ctx->quic));
                                if (ret != 0) {
                                    DBG_PRINTF("Datagram ack init returns %d", ret);
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    else {
        /* not ready yet */
        *not_ready = 1;
    }

    return ret;
}

int quicrq_relay_datagram_publisher_fn(
    quicrq_stream_ctx_t* stream_ctx,
    void* context,
    size_t space,
    int* media_was_sent,
    int* at_least_one_active)
{
    int ret = 0;
    int not_ready = 0;
    quicrq_relay_publisher_context_t* media_ctx = (quicrq_relay_publisher_context_t*)stream_ctx->media_ctx;

    /* The "prepare" function has no dependency on stream context,
     * which helps designing unit tests.
     */
    ret = quicrq_relay_datagram_publisher_prepare(stream_ctx, media_ctx,
        stream_ctx->datagram_stream_id, context, space, media_was_sent, at_least_one_active, &not_ready);

    if (not_ready){
        /* Nothing to send at this point. If the media sending is finished, mark the stream accordingly.
         * The cache filling function checks that the final ID is only marked when all fragments have been
         * received. At this point, we only check that the final ID is marked, and all fragments have 
         * been sent.
         */

        if (media_ctx->cache_ctx->final_object_id != 0 &&
            media_ctx->current_fragment != NULL &&
            media_ctx->length_sent >= media_ctx->current_fragment->data_length &&
            media_ctx->current_fragment->next_in_order == NULL) {
            /* Mark the stream as finished, prepare sending a final message */
            stream_ctx->final_object_id = media_ctx->cache_ctx->final_object_id;
            /* Wake up the control stream so the final message can be sent. */
            picoquic_mark_active_stream(stream_ctx->cnx_ctx->cnx, stream_ctx->stream_id, 1, stream_ctx);
            stream_ctx->is_active_datagram = 0;
        }
    }

    return ret;
}

void* quicrq_relay_publisher_subscribe(void* v_srce_ctx, quicrq_stream_ctx_t * stream_ctx)
{
    quicrq_relay_cached_media_t* cache_ctx = (quicrq_relay_cached_media_t*)v_srce_ctx;
    quicrq_relay_publisher_context_t* media_ctx = (quicrq_relay_publisher_context_t*)
        malloc(sizeof(quicrq_relay_publisher_context_t));
    if (media_ctx != NULL) {
        memset(media_ctx, 0, sizeof(quicrq_relay_publisher_context_t));
        media_ctx->cache_ctx = cache_ctx;
        if (stream_ctx != NULL) {
            stream_ctx->start_object_id = cache_ctx->first_object_id;
        }
    }
    return media_ctx;
}

void quicrq_relay_publisher_delete(void* v_pub_ctx)
{
    quicrq_relay_cached_media_t* cache_ctx = (quicrq_relay_cached_media_t*)v_pub_ctx;
    quicrq_relay_cache_media_clear(cache_ctx);
    free(cache_ctx);
}

/* Default source is called when a client of a relay is loading a not-yet-cached
 * URL. This requires creating the desired URL, and then opening the stream to
 * the server. Possibly, starting a connection if there is no server available.
 */

int quicrq_relay_check_server_cnx(quicrq_relay_context_t* relay_ctx, quicrq_ctx_t* qr_ctx)
{
    int ret = 0;
    /* If there is no valid connection to the server, create one. */
    /* TODO: check for expiring connection */
    if (relay_ctx->cnx_ctx == NULL) {
        relay_ctx->cnx_ctx = quicrq_create_client_cnx(qr_ctx, relay_ctx->sni,
            (struct sockaddr*)&relay_ctx->server_addr);
    }
    if (relay_ctx->cnx_ctx == NULL) {
        ret = -1;
    }
    return ret;
}

quicrq_relay_cached_media_t* quicrq_relay_create_cache_ctx()
{
    quicrq_relay_cached_media_t* cache_ctx = (quicrq_relay_cached_media_t*)malloc(
        sizeof(quicrq_relay_cached_media_t));
    if (cache_ctx != NULL) {
        memset(cache_ctx, 0, sizeof(quicrq_relay_cached_media_t));
        cache_ctx->subscribe_stream_id = UINT64_MAX;
        quicrq_relay_cache_media_init(cache_ctx);
    }
    return cache_ctx;
}

void quicrq_relay_delete_cache(quicrq_relay_cached_media_t* cache_ctx)
{
    return;
}

quicrq_relay_consumer_context_t* quicrq_relay_create_cons_ctx()
{
    quicrq_relay_consumer_context_t* cons_ctx = (quicrq_relay_consumer_context_t*)
        malloc(sizeof(quicrq_relay_consumer_context_t));
    if (cons_ctx != NULL) {
        memset(cons_ctx, 0, sizeof(quicrq_relay_consumer_context_t));
    }
    return cons_ctx;
}

int quicrq_relay_publish_cached_media(quicrq_ctx_t* qr_ctx,
    quicrq_relay_cached_media_t* cache_ctx, const uint8_t* url, const size_t url_length)
{
    /* if succeeded, publish the source */
    cache_ctx->srce_ctx = quicrq_publish_datagram_source(qr_ctx, url, url_length, cache_ctx,
        quicrq_relay_publisher_subscribe, quicrq_relay_publisher_fn, quicrq_relay_datagram_publisher_fn,
        quicrq_relay_publisher_delete);
    return (cache_ctx->srce_ctx == NULL)?-1:0;
}

int quicrq_relay_default_source_fn(void* default_source_ctx, quicrq_ctx_t* qr_ctx,
    const uint8_t* url, const size_t url_length)
{
    int ret = 0;
    /* Should there be a single context type for relays and origin? */
    quicrq_relay_context_t* relay_ctx = (quicrq_relay_context_t*)default_source_ctx;
    if (url == NULL) {
        /* By convention, this is a request to release the resource of the origin */
        quicrq_set_default_source(qr_ctx, NULL, NULL);
    }
    else {
        quicrq_relay_cached_media_t* cache_ctx = quicrq_relay_create_cache_ctx();
        quicrq_relay_consumer_context_t* cons_ctx = NULL;
        if (cache_ctx == NULL) {
            ret = -1;
        }
        else if (!relay_ctx->is_origin_only) {
            /* If there is no valid connection to the server, create one. */
            ret = quicrq_relay_check_server_cnx(relay_ctx, qr_ctx);

            if (ret == 0) {
                /* Create a consumer context for the relay to server connection */
                cons_ctx = quicrq_relay_create_cons_ctx();

                if (cons_ctx == NULL) {
                    ret = -1;
                }
                else {
                    cons_ctx->cached_ctx = cache_ctx;

                    /* Request a URL on a new stream on that connection */
                    ret = quicrq_cnx_subscribe_media(relay_ctx->cnx_ctx, url, url_length,
                        relay_ctx->use_datagrams, quicrq_relay_consumer_cb, cons_ctx);
                    if (ret == 0){
                        /* Document the stream ID for that cache */
                        char buffer[256];
                        cache_ctx->subscribe_stream_id = relay_ctx->cnx_ctx->last_stream->stream_id; 
                        picoquic_log_app_message(relay_ctx->cnx_ctx->cnx, "Asking server for URL: %s on stream %" PRIu64,
                            quicrq_uint8_t_to_text(url, url_length, buffer, 256), cache_ctx->subscribe_stream_id);
                    }
                }
            }
        }
        else {
            /* TODO: whatever is needed for origin only behavior. */
        }

        if (ret == 0) {
            /* if succeeded, publish the source */
            ret = quicrq_relay_publish_cached_media(qr_ctx, cache_ctx, url, url_length);
        }

        if (ret != 0) {
            if (cache_ctx != NULL) {
                free(cache_ctx);
            }
            if (cons_ctx != NULL) {
                free(cons_ctx);
            }
        }
    }
    return ret;
}

/* The relay consumer callback is called when receiving a "post" request from
 * a client. It will initialize a cached media context for the posted url.
 * The media will be received on the specified stream, as either stream or datagram.
 * The media shall be stored in a local cache entry.
 * The cached entry shall be pushed on a connection to the server.
 */

int quicrq_relay_consumer_init_callback(quicrq_stream_ctx_t* stream_ctx, const uint8_t* url, size_t url_length)
{
    int ret = 0;
    quicrq_ctx_t* qr_ctx = stream_ctx->cnx_ctx->qr_ctx;
    quicrq_relay_context_t* relay_ctx = (quicrq_relay_context_t*)qr_ctx->default_source_ctx;

    quicrq_relay_cached_media_t* cache_ctx = NULL;
    quicrq_relay_consumer_context_t* cons_ctx = NULL;

    /* If there is no valid connection to the server, create one. */
    ret = quicrq_relay_check_server_cnx(relay_ctx, qr_ctx);
    if (ret == 0) {
        quicrq_media_source_ctx_t* srce_ctx = quicrq_find_local_media_source(qr_ctx, url, url_length);

        if (srce_ctx != NULL) {
            cache_ctx = (quicrq_relay_cached_media_t*)srce_ctx->pub_ctx;
            if (cache_ctx == NULL) {
                ret = -1;
            }
            else {
                /* Abandon the stream that was open to receive the media */
                char buffer[256];
                quicrq_cnx_abandon_stream_id(relay_ctx->cnx_ctx, cache_ctx->subscribe_stream_id);
                picoquic_log_app_message(stream_ctx->cnx_ctx->cnx, "Abandon subscription to URL: %s",
                    quicrq_uint8_t_to_text(url, url_length, buffer, 256));
            }
        }
        else {
            /* Create a cache context for the URL */
            cache_ctx = quicrq_relay_create_cache_ctx();
            if (cache_ctx != NULL) {
                char buffer[256];
                ret = quicrq_relay_publish_cached_media(qr_ctx, cache_ctx, url, url_length);
                picoquic_log_app_message(stream_ctx->cnx_ctx->cnx, "Create cache for URL: %s",
                    quicrq_uint8_t_to_text(url, url_length, buffer, 256));
                if (ret != 0) {
                    /* Could not publish the media, free the resource. */
                    free(cache_ctx);
                    cache_ctx = NULL;
                    ret = -1;
                }
            }
        }

        if (ret == 0) {
            cons_ctx = quicrq_relay_create_cons_ctx();
            if (cons_ctx == NULL) {
                ret = -1;
            }
            else {
                ret = quicrq_cnx_post_media(relay_ctx->cnx_ctx, url, url_length, relay_ctx->use_datagrams);
                if (ret != 0) {
                    /* TODO: unpublish the media context */
                    DBG_PRINTF("Should unpublish media context, ret = %d", ret);
                }
                else {
                    /* set the parameter in the stream context. */
                    char buffer[256];

                    cons_ctx->cached_ctx = cache_ctx;
                    ret = quicrq_set_media_stream_ctx(stream_ctx, quicrq_relay_consumer_cb, cons_ctx);
                    picoquic_log_app_message(stream_ctx->cnx_ctx->cnx, "Posting URL: %s to server on stream %" PRIu64,
                        quicrq_uint8_t_to_text(url, url_length, buffer, 256), stream_ctx->stream_id);
                }
            }
        }
    }

    return ret;
}

/* The relay functionality has to be established to add the relay
 * function to a QUICRQ node.
 */
int quicrq_enable_relay(quicrq_ctx_t* qr_ctx, const char* sni, const struct sockaddr* addr, int use_datagrams)
{
    int ret = 0;

    if (qr_ctx->relay_ctx != NULL) {
        /* Error -- cannot enable relaying twice without first disabling it */
        ret = -1;
    }
    else {
        size_t sni_len = (sni == NULL) ? 0 : strlen(sni);
        quicrq_relay_context_t* relay_ctx = (quicrq_relay_context_t*)malloc(
            sizeof(quicrq_relay_context_t) + sni_len + 1);
        if (relay_ctx == NULL) {
            ret = -1;
        }
        else {
            /* initialize the relay context. */
            uint8_t* v_sni = ((uint8_t*)relay_ctx) + sizeof(quicrq_relay_context_t);
            memset(relay_ctx, 0, sizeof(quicrq_relay_context_t));
            picoquic_store_addr(&relay_ctx->server_addr, addr);
            if (sni_len > 0) {
                memcpy(v_sni, sni, sni_len);
            }
            v_sni[sni_len] = 0;
            relay_ctx->sni = (char const*)v_sni;
            relay_ctx->use_datagrams = use_datagrams;
            /* set the relay as default provider */
            quicrq_set_default_source(qr_ctx, quicrq_relay_default_source_fn, relay_ctx);
            /* set a default post client on the relay */
            quicrq_set_media_init_callback(qr_ctx, quicrq_relay_consumer_init_callback);
            qr_ctx->relay_ctx = relay_ctx;
            qr_ctx->manage_relay_cache_fn = quicrq_manage_relay_cache;
        }
    }
    return ret;
}

void quicrq_disable_relay(quicrq_ctx_t* qr_ctx)
{
    if (qr_ctx->relay_ctx != NULL) {
        free(qr_ctx->relay_ctx);
        qr_ctx->relay_ctx = NULL;
        qr_ctx->manage_relay_cache_fn = NULL;
    }
}

/* Management of the relay cache
 */
void quicrq_manage_relay_cache(quicrq_ctx_t* qr_ctx, uint64_t current_time)
{
    if (qr_ctx->relay_ctx != NULL && qr_ctx->cache_duration_max > 0) {
        quicrq_media_source_ctx_t* srce_ctx = qr_ctx->first_source;

        /* Find all the sources that are cached by the relay function */
        while (srce_ctx != NULL) {
            quicrq_media_source_ctx_t* srce_to_delete = NULL;
            if (srce_ctx->subscribe_fn == quicrq_relay_publisher_subscribe &&
                srce_ctx->getdata_fn == quicrq_relay_publisher_fn &&
                srce_ctx->get_datagram_fn == quicrq_relay_datagram_publisher_fn &&
                srce_ctx->delete_fn == quicrq_relay_publisher_delete) {
                /* This is a source created by the relay */
                quicrq_relay_cached_media_t* cache_ctx = (quicrq_relay_cached_media_t*)srce_ctx->pub_ctx;
                /* TODO: Check the lowest value of the published object along subscribed clients;
                 * setting the lowest value to UIN64_MAX for now */
                /* Purge cache from old entries */
                quicrq_relay_cache_media_purge(cache_ctx,
                    current_time, qr_ctx->cache_duration_max, UINT64_MAX);
                /* If the cache is empty and the source is closed, schedule it for deletion. */
                if (cache_ctx->first_fragment == NULL &&
                    cache_ctx->is_closed) {
                    srce_to_delete = srce_ctx;
                }
            }
            srce_ctx = srce_ctx->next_source;
            if (srce_to_delete != NULL) {
                quicrq_delete_source(srce_to_delete, qr_ctx);
            }
        }
    }
}

/*
 * The origin server behavior is very similar to the behavior of a realy, but
 * there are some key differences:
 *  
 *  1) When receiving a "subscribe" request, the relay creates the media context and
 *     starts a connection. The server creates a media context but does not start the connection. 
 *  2) When receiving a "post" request, the relay creates a cache version and also
 *     forwards it to the server using an upload connection. There is no upload
 *     connection at the origin server. 
 *  3) When receiving a "post" request, the server must check whether the media context 
 *     already exists, and if it does connects it.
 */

int quicrq_origin_consumer_init_callback(quicrq_stream_ctx_t* stream_ctx, const uint8_t* url, size_t url_length)
{
    int ret = 0;
    quicrq_ctx_t* qr_ctx = stream_ctx->cnx_ctx->qr_ctx;
    quicrq_relay_cached_media_t* cache_ctx = NULL;
    quicrq_relay_consumer_context_t* cons_ctx = quicrq_relay_create_cons_ctx();
    char buffer[256];

    if (cons_ctx == NULL) {
        ret = -1;
    } else {
        /* Check whether there is already a context for this media */
        quicrq_media_source_ctx_t* srce_ctx = quicrq_find_local_media_source(qr_ctx, url, url_length);

        if (srce_ctx != NULL) {
            cache_ctx = (quicrq_relay_cached_media_t*)srce_ctx->pub_ctx;
            picoquic_log_app_message(stream_ctx->cnx_ctx->cnx, "Found cache context for URL: %s",
                quicrq_uint8_t_to_text(url, url_length, buffer, 256));
        }
        else {
            /* Create a cache context for the URL */
            cache_ctx = quicrq_relay_create_cache_ctx();
            if (cache_ctx != NULL) {
                ret = quicrq_relay_publish_cached_media(qr_ctx, cache_ctx, url, url_length);
                if (ret != 0) {
                    /* Could not publish the media, free the resource. */
                    free(cache_ctx);
                    cache_ctx = NULL;
                    picoquic_log_app_message(stream_ctx->cnx_ctx->cnx, "Cannot create cache for URL: %s",
                        quicrq_uint8_t_to_text(url, url_length, buffer, 256));
                }
                else {
                    picoquic_log_app_message(stream_ctx->cnx_ctx->cnx, "Created cache context for URL: %s",
                        quicrq_uint8_t_to_text(url, url_length, buffer, 256));
                }
            }
        }

        if (ret == 0) {
            /* set the parameter in the stream context. */
            cons_ctx->cached_ctx = cache_ctx;
            ret = quicrq_set_media_stream_ctx(stream_ctx, quicrq_relay_consumer_cb, cons_ctx);
        }

        if (ret != 0) {
            free(cons_ctx);
        }
    }
    return ret;
}

int quicrq_enable_origin(quicrq_ctx_t* qr_ctx, int use_datagrams)
{
    int ret = 0;
    quicrq_relay_context_t* relay_ctx = (quicrq_relay_context_t*)malloc(
        sizeof(quicrq_relay_context_t));
    if (relay_ctx == NULL) {
        ret = -1;
    }
    else {
        /* initialize the relay context. */
        memset(relay_ctx, 0, sizeof(quicrq_relay_context_t));
        relay_ctx->use_datagrams = use_datagrams;
        relay_ctx->is_origin_only = 1;
        /* set the relay as default provider */
        quicrq_set_default_source(qr_ctx, quicrq_relay_default_source_fn, relay_ctx);
        /* set a default post client on the relay */
        quicrq_set_media_init_callback(qr_ctx, quicrq_origin_consumer_init_callback);
        /* Remember pointer */
        qr_ctx->relay_ctx = relay_ctx;
    }
    return ret;
}
