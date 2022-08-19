/* Handling of the fragment cache */
#ifndef QUICRQ_FRAGMENT_H
#define QUICRQ_FRAGMENT_H

#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include "picoquic_utils.h"
#include "picosplay.h"
#include "quicrq.h"
#include "quicrq_reassembly.h"
#include "quicrq_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct st_quicrq_cached_fragment_t {
    picosplay_node_t fragment_node;
    uint64_t group_id;
    uint64_t object_id;
    uint64_t offset;
    uint64_t cache_time;
    uint64_t queue_delay;
    uint64_t nb_objects_previous_group;
    uint8_t flags;
    int is_last_fragment;
    struct st_quicrq_cached_fragment_t* previous_in_order;
    struct st_quicrq_cached_fragment_t* next_in_order;
    size_t data_length;
    uint8_t* data;
} quicrq_cached_fragment_t;

typedef struct st_quicrq_fragment_cached_media_t {
    quicrq_media_source_ctx_t* srce_ctx;
    quicrq_ctx_t* qr_ctx;
    uint64_t final_group_id;
    uint64_t final_object_id;
    uint64_t nb_object_received;
    uint64_t subscribe_stream_id;
    uint64_t first_group_id;
    uint64_t first_object_id;
    uint64_t next_group_id;
    uint64_t next_object_id;
    uint64_t next_offset;
    quicrq_cached_fragment_t* first_fragment;
    quicrq_cached_fragment_t* last_fragment;
    picosplay_tree_t fragment_tree;
    int is_closed;
    uint64_t cache_delete_time;
} quicrq_fragment_cached_media_t;

typedef struct st_quicrq_fragment_publisher_object_state_t {
    picosplay_node_t publisher_object_node;
    uint64_t group_id;
    uint64_t object_id;
    uint64_t nb_objects_previous_group;
    uint64_t final_offset;
    uint64_t bytes_sent;
    int is_dropped;
    int is_sent;
} quicrq_fragment_publisher_object_state_t;

typedef struct st_quicrq_fragment_publisher_context_t {
    quicrq_fragment_cached_media_t* cache_ctx;
    uint64_t current_group_id;
    uint64_t current_object_id;
    size_t current_offset;
    int is_object_complete;
    int is_media_complete;
    int is_sending_object;
    int is_start_point_sent;
    int is_current_object_skipped;
    int has_backlog;
    quicrq_cached_fragment_t* current_fragment;
    uint64_t length_sent;
    int is_current_fragment_sent;
    picosplay_tree_t publisher_object_tree;
} quicrq_fragment_publisher_context_t;

void* quicrq_fragment_cache_node_value(picosplay_node_t* fragment_node);

quicrq_cached_fragment_t* quicrq_fragment_cache_get_fragment(quicrq_fragment_cached_media_t* cached_ctx,
    uint64_t group_id, uint64_t object_id, uint64_t offset);

void quicrq_fragment_cache_media_clear(quicrq_fragment_cached_media_t* cached_media);

void quicrq_fragment_cache_media_init(quicrq_fragment_cached_media_t* cached_media);

/* Fragment cache progress.
 * Manage the "next_group" and "next_object" items.
 */
void quicrq_fragment_cache_progress(quicrq_fragment_cached_media_t* cached_ctx,
    quicrq_cached_fragment_t* fragment);

int quicrq_fragment_add_to_cache(quicrq_fragment_cached_media_t* cached_ctx,
    const uint8_t* data,
    uint64_t group_id,
    uint64_t object_id,
    uint64_t offset,
    uint64_t queue_delay,
    uint8_t flags,
    uint64_t nb_objects_previous_group,
    int is_last_fragment,
    size_t data_length,
    uint64_t current_time);

int quicrq_fragment_propose_to_cache(quicrq_fragment_cached_media_t* cached_ctx,
    const uint8_t* data,
    uint64_t group_id,
    uint64_t object_id,
    uint64_t offset,
    uint64_t queue_delay,
    uint8_t flags,
    uint64_t nb_objects_previous_group,
    int is_last_fragment,
    size_t data_length,
    uint64_t current_time);

int quicrq_fragment_cache_learn_start_point(quicrq_fragment_cached_media_t* cached_ctx,
    uint64_t start_group_id, uint64_t start_object_id);

int quicrq_fragment_cache_learn_end_point(quicrq_fragment_cached_media_t* cached_ctx, uint64_t final_group_id, uint64_t final_object_id);

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

void quicrq_fragment_cache_media_purge(
    quicrq_fragment_cached_media_t* cached_media,
    uint64_t current_time,
    uint64_t cache_duration_max,
    uint64_t first_object_id_kept);

void quicrq_fragment_cache_delete_ctx(quicrq_fragment_cached_media_t* cache_ctx);

quicrq_fragment_cached_media_t* quicrq_fragment_cache_create_ctx(quicrq_ctx_t* qr_ctx);

/* Fragment publisher
 * 
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

quicrq_fragment_publisher_object_state_t* quicrq_fragment_publisher_object_add(quicrq_fragment_publisher_context_t* media_ctx,
    uint64_t group_id, uint64_t object_id);

quicrq_fragment_publisher_object_state_t* quicrq_fragment_publisher_object_get(quicrq_fragment_publisher_context_t* media_ctx,
    uint64_t group_id, uint64_t object_id);

void quicrq_fragment_publisher_close(quicrq_fragment_publisher_context_t* media_ctx);

int quicrq_fragment_publisher_fn(
    quicrq_media_source_action_enum action,
    void* v_media_ctx,
    uint8_t* data,
    size_t data_max_size,
    size_t* data_length,
    uint8_t* flags,
    int* is_new_group,
    int* is_last_fragment,
    int* is_media_finished,
    int* is_still_active,
    int* has_backlog,
    uint64_t current_time);


/* Evaluate whether the media context has backlog, and check
* whether the current object should be skipped.
*/
int quicrq_fragment_datagram_publisher_object_eval(
    quicrq_stream_ctx_t* stream_ctx,
    quicrq_fragment_publisher_context_t* media_ctx, int* should_skip, uint64_t current_time);


/* datagram_publisher_check_object:
 * evaluate and if necessary progress the "current fragment" pointer.
 * After this evaluation, expect the following results:
 *  - return code not zero: something went very wrong.
 *  - media_ctx->current_fragment == NULL: sending is not started yet.
 *  - media_ctx->current_fragment != NULL:
 *    - media_ctx->is_current_fragment_sent == 1: already sent. Nothing else available.
 *    - media_ctx->is_current_fragment_sent == 0: should be processed
 */

int quicrq_fragment_datagram_publisher_check_fragment(
    quicrq_stream_ctx_t* stream_ctx, quicrq_fragment_publisher_context_t* media_ctx, int* should_skip, uint64_t current_time);

/* Prune the publisher object tree, removing all nodes that
 * have a successor and have not already been sent.
 * This avoids keeping large lists in memory.
 */
int quicrq_fragment_datagram_publisher_object_prune(
    quicrq_fragment_publisher_context_t* media_ctx);

/* Update the publisher object after a fragment was sent.
 * - Keep track of how many bytes were sent for the object.
 * - Keep track of the bytes needed:
 *   - zero if object is skipped
 *   - final offset if object is sent.
 * - Mark "sent" if all bytes sent.
 * - if sent, check whether to prune the tree
 */
int quicrq_fragment_datagram_publisher_object_update(
    quicrq_fragment_publisher_context_t* media_ctx,
    int should_skip,
    int is_last_fragment,
    uint64_t next_offset,
    size_t copied);

/* Send the next fragment, or a placeholder if the object shall be skipped. 
 */
int quicrq_fragment_datagram_publisher_send_fragment(
    quicrq_stream_ctx_t* stream_ctx,
    quicrq_fragment_publisher_context_t* media_ctx,
    uint64_t datagram_stream_id,
    void* context,
    size_t space,
    int* media_was_sent,
    int* at_least_one_active,
    int should_skip);

int quicrq_fragment_datagram_publisher_prepare(
    quicrq_stream_ctx_t* stream_ctx,
    quicrq_fragment_publisher_context_t* media_ctx,
    uint64_t datagram_stream_id,
    void* context,
    size_t space,
    int* media_was_sent,
    int* at_least_one_active,
    int* not_ready,
    uint64_t current_time);

int quicrq_fragment_datagram_publisher_fn(
    quicrq_stream_ctx_t* stream_ctx,
    void* context,
    size_t space,
    int* media_was_sent,
    int* at_least_one_active,
    uint64_t current_time);

void* quicrq_fragment_publisher_subscribe(void* v_srce_ctx, quicrq_stream_ctx_t* stream_ctx);

void quicrq_fragment_publisher_delete(void* v_pub_ctx);

/* Fragment cache media publish */
int quicrq_publish_fragment_cached_media(quicrq_ctx_t* qr_ctx,
    quicrq_fragment_cached_media_t* cache_ctx, const uint8_t* url, const size_t url_length,
    int is_local_object_source);

#ifdef __cplusplus
}
#endif

#endif /* QUICRQ_FRAGMENT_H */