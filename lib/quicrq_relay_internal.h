/* Handling of a relay
 */
#ifndef QUICRQ_INTERNAL_RELAY_H
#define QUICRQ_INTERNAL_RELAY_H

#include "picosplay.h"
#include "quicrq.h"
#include "quicrq_reassembly.h"
#include "quicrq_internal.h"
#include "quicrq_relay.h"

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

 /* Relay definitions.
  * When acting as a client, the relay adds the media to a cache, which can then be read by the
  * server part of the relay. The publisher state indicates the current object being read from the
  * cache, and the state of the response.
  *
  * In the current version, the cached media is represented in memory by an array of objects,
  * identified by a object number. We may need to add metadata later,such as adding a timestamp
  * to a object, or marking objects as potential restart point, potential skip on restart,
  * and maybe an indication of encoding layer.
  *
  * The objects are added when received on a client connection, organized as a binary tree.
  * The relayed media is kept until the end of the relay connection. This is of course not
  * sustainable, some version of cache management will have to be added later.
  */

typedef struct st_quicrq_relay_cached_fragment_t {
    picosplay_node_t fragment_node;
    uint64_t group_id;
    uint64_t object_id;
    uint64_t offset;
    uint64_t cache_time;
    uint64_t queue_delay;
    uint64_t nb_objects_previous_group;
    uint8_t flags;
    int is_last_fragment;
    struct st_quicrq_relay_cached_fragment_t* previous_in_order;
    struct st_quicrq_relay_cached_fragment_t* next_in_order;
    size_t data_length;
    uint8_t* data;
} quicrq_relay_cached_fragment_t;

typedef struct st_quicrq_relay_cached_media_t {
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
    quicrq_relay_cached_fragment_t* first_fragment;
    quicrq_relay_cached_fragment_t* last_fragment;
    picosplay_tree_t fragment_tree;
    int is_closed;
    uint64_t cache_delete_time;
} quicrq_relay_cached_media_t;

typedef struct st_quicrq_relay_publisher_context_t {
    quicrq_relay_cached_media_t* cache_ctx;
    uint64_t current_group_id;
    uint64_t current_object_id;
    size_t current_offset;
    int is_object_complete;
    int is_media_complete;
    int is_sending_object;
    int is_start_point_sent;
    int is_current_object_skipped;
    int has_backlog;
    quicrq_relay_cached_fragment_t* current_fragment;
    uint64_t length_sent;
    int is_current_fragment_sent;
} quicrq_relay_publisher_context_t;

typedef struct st_quicrq_relay_consumer_context_t {
    uint64_t last_object_id;
    quicrq_relay_cached_media_t* cached_ctx;
} quicrq_relay_consumer_context_t;

typedef struct st_quicrq_relay_context_t {
    const char* sni;
    struct sockaddr_storage server_addr;
    quicrq_ctx_t* qr_ctx;
    quicrq_cnx_ctx_t* cnx_ctx;
    int is_origin_only : 1;
    int use_datagrams : 1;
} quicrq_relay_context_t;

int quicrq_relay_propose_fragment_to_cache(quicrq_relay_cached_media_t* cached_ctx,
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
quicrq_relay_cached_fragment_t* quicrq_relay_cache_get_fragment(quicrq_relay_cached_media_t* cached_ctx, 
    uint64_t group_id, uint64_t object_id, uint64_t offset);

int quicrq_relay_datagram_publisher_prepare(
    quicrq_stream_ctx_t* stream_ctx,
    quicrq_relay_publisher_context_t* media_ctx,
    uint64_t datagram_stream_id,
    void* context,
    size_t space,
    int* media_was_sent,
    int* at_least_one_active,
    int* not_ready);

int quicrq_relay_datagram_publisher_fn(
    quicrq_stream_ctx_t* stream_ctx,
    void* context,
    size_t space,
    int* media_was_sent,
    int* at_least_one_active);

int quicrq_relay_publisher_fn(
    quicrq_media_source_action_enum action,
    void* v_media_ctx,
    uint8_t* data,
    size_t data_max_size,
    size_t* data_length,
    uint8_t* flags,
    int * is_new_group,
    int* is_last_fragment,
    int* is_media_finished,
    int* is_still_active,
    int* has_backlog,
    uint64_t current_time);

quicrq_relay_cached_media_t* quicrq_relay_create_cache_ctx(quicrq_ctx_t * qr_ctx);
void quicrq_relay_delete_cache_ctx(quicrq_relay_cached_media_t* cache_ctx);

/* Management of the relay cache
 */
uint64_t quicrq_manage_relay_cache(quicrq_ctx_t* qr_ctx, uint64_t current_time);

#endif /* QUICRQ_INTERNAL_RELAY_H */
