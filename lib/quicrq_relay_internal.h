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
 * segment and as server when producing data.
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
 * The client half creates a list of media frames. For simplification, the server half will
 * only deal with the media frames that are fully received. When a media frame is
 * fully received, it becomes available. We may consider a difference in
 * availability between "in-order" and "out-of-sequence" availablity, which
 * may need to be reflected in the contract between connection and sources.
 */

 /* Relay definitions.
  * When acting as a client, the relay adds the media to a cache, which can then be read by the
  * server part of the relay. The publisher state indicates the current frame being read from the
  * cache, and the state of the response.
  *
  * In the current version, the cached media is represented in memory by an array of frames,
  * identified by a frame number. We may need to add metadata later,such as adding a timestamp
  * to a frame, or marking frames as potential restart point, potential skip on restart,
  * and maybe an indication of encoding layer.
  *
  * The frames are added when received on a client connection, organized as a binary tree.
  * The relayed media is kept until the end of the relay connection. This is of course not
  * sustainable, some version of cache management will have to be added later.
  */

#if 1
typedef struct st_quicrq_relay_cached_segment_t {
    picosplay_node_t segment_node;
    uint64_t frame_id;
    uint64_t offset;
    int is_last_segment;
    struct st_quicrq_relay_cached_segment_t* next_in_order;
    size_t data_length;
    uint8_t* data;
} quicrq_relay_cached_segment_t;

#else
typedef struct st_quicrq_relay_cached_frame_t {
    picosplay_node_t frame_node;
    uint64_t frame_id;
    uint8_t* data;
    size_t data_length;
} quicrq_relay_cached_frame_t;
#endif

typedef struct st_quicrq_relay_cached_media_t {
    quicrq_media_source_ctx_t* srce_ctx;
    uint64_t final_frame_id;
    uint64_t nb_frame_received;
    uint64_t subscribe_stream_id;
#if 1
    quicrq_relay_cached_segment_t* first_segment;
    quicrq_relay_cached_segment_t* last_segment;
    picosplay_tree_t segment_tree;
#else 
    picosplay_tree_t frame_tree;
#endif
} quicrq_relay_cached_media_t;

typedef struct st_quicrq_relay_publisher_context_t {
    quicrq_relay_cached_media_t* cache_ctx;
    uint64_t current_frame_id;
    size_t current_offset;
    int is_frame_complete;
    int is_media_complete;
    int is_sending_frame;
#if 1
    quicrq_relay_cached_segment_t* current_segment;
    uint64_t length_sent;
#else
    quicrq_sent_frame_ranges_t ranges;
#endif
} quicrq_relay_publisher_context_t;

typedef struct st_quicrq_relay_consumer_context_t {
    uint64_t last_frame_id;
    quicrq_relay_cached_media_t* cached_ctx;
#if 1
#else
    quicrq_reassembly_context_t reassembly_ctx;
#endif
} quicrq_relay_consumer_context_t;

typedef struct st_quicrq_relay_context_t {
    const char* sni;
    struct sockaddr_storage server_addr;
    quicrq_ctx_t* qr_ctx;
    quicrq_cnx_ctx_t* cnx_ctx;
    int is_origin_only : 1;
    int use_datagrams : 1;
} quicrq_relay_context_t;


int quicrq_relay_add_frame_to_cache(quicrq_relay_cached_media_t* cached_ctx,
    uint64_t frame_id,
    const uint8_t* data,
    size_t data_length);
int quicrq_relay_next_available_frame_id(quicrq_sent_frame_ranges_t* frame_ranges, quicrq_relay_cached_media_t* cached_ctx, uint64_t* next_frame_id, int* is_finished);
int quicrq_relay_add_frame_id_to_ranges(quicrq_sent_frame_ranges_t* frame_ranges, uint64_t frame_id);
quicrq_relay_cached_media_t* quicrq_relay_create_cache_ctx();
void quicrq_relay_delete_cache_ctx(quicrq_relay_cached_media_t* cache_ctx);
void quick_relay_clear_ranges(quicrq_sent_frame_ranges_t* frame_ranges);

#endif /* QUICRQ_INTERNAL_RELAY_H */