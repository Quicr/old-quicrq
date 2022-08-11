/*
* Author: Christian Huitema
* Copyright (c) 2019, Private Octopus, Inc.
* All rights reserved.
*
* Permission to use, copy, modify, and distribute this software for any
* purpose with or without fee is hereby granted, provided that the above
* copyright notice and this permission notice appear in all copies.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
* ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL Private Octopus, Inc. BE LIABLE FOR ANY
* DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
* (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
* LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
* ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
* SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef quicrq_internal_H
#define quicrq_internal_H

#include "picoquic.h"
#include "picosplay.h"
#include "quicrq.h"

#ifdef __cplusplus
extern "C" {
#endif

#define QUICRQ_MAX_CONNECTIONS 256

/* Implementation of the quicrq application on top of picoquic. 
 * 
 * The quicrq context is created by the call to quicrq_create, which
 * starts the operation. It is deleted by a call to quicr_delete */


/* Protocol message buffer.
 * For the base protocol, all messages start with a 2-bytes length field,
 * and are accumulated in a quicrq_incoming_message buffer.
 */
typedef struct st_quicrq_message_buffer_t {
    size_t nb_bytes_read; /* if >= 2, the message size is known */
    size_t message_size;
    size_t buffer_alloc;
    uint8_t* buffer;
    int is_finished;
} quicrq_message_buffer_t;

int quicrq_msg_buffer_alloc(quicrq_message_buffer_t* msg_buffer, size_t space, size_t bytes_stored);
uint8_t* quicrq_msg_buffer_store(uint8_t* bytes, size_t length, quicrq_message_buffer_t* msg_buffer, int* is_finished);
void quicrq_msg_buffer_reset(quicrq_message_buffer_t* msg_buffer);
void quicrq_msg_buffer_release(quicrq_message_buffer_t* msg_buffer);

/* The protocol used for our tests defines a set of actions:
 * - Open Stream: request to open a stream, defined by URL of media fragment. Content will be sent as a stream of bytes.
 * - Open datagram: same as open stream, but specifying opening as a "datagram" stream and providing the ID of that stream.
 *   Content will be sent as a stream of datagrams, each specifying an offset and a set of bytes.
 * - Fin Datagram: when the media fragment has been sent as a set of datagrams, provides the final offset.
 * - Request repair: when a stream is opened as datagram, some datagrams may be lost. The receiver may request data at offset and length.
 * - Repair: 1 byte code, followed by content of a datagram
 */
#define QUICRQ_ACTION_REQUEST_STREAM 1
#define QUICRQ_ACTION_REQUEST_DATAGRAM 2
#define QUICRQ_ACTION_FIN_DATAGRAM 3
#define QUICRQ_ACTION_REQUEST_REPAIR 4
#define QUICRQ_ACTION_FRAGMENT 5
#define QUICRQ_ACTION_POST 6
#define QUICRQ_ACTION_ACCEPT 7
#define QUICRQ_ACTION_START_POINT 8
#define QUICRQ_ACTION_SUBSCRIBE 9
#define QUICRQ_ACTION_NOTIFY 10

/* Protocol message.
 * This structure is used when decoding messages
 */
typedef struct st_quicrq_message_t {
    uint64_t message_type;
    size_t url_length;
    const uint8_t* url;
    uint64_t datagram_stream_id;
    uint64_t group_id;
    uint64_t object_id;
    uint64_t nb_objects_previous_group;
    uint64_t offset;
    uint8_t flags;
    int is_last_fragment;
    size_t length;
    const uint8_t* data;
    unsigned int use_datagram;
} quicrq_message_t;

/* Encode and decode protocol messages
 * 
 * The protocol defines a set of actions, identified by a code.
 * 
 * - rq_msg: request message, ask for a media identified by an URL
 * - fin_msg: signal the last obect identifier in the media flow
 * - repair_request: require repeat of a specific object fragment (not used yet)
 * - repair_msg: provide the value of a specific fragment
 * - quicr_msg: generic message, with type and value specified inside "msg" argument
 * 
 * For each action we get a specific encoding, decoding, and size reservation function.
 * The "*_reserve" predict the size of the buffer required for encoding
 * the message. A typical flow would be:
 * 
 * - use xxxx_reserve and estimate the size
 * - allocate a buffer with at least that size
 * - encode the message using xxxx_encode
 */
size_t quicrq_subscribe_msg_reserve(size_t url_length);
uint8_t* quicrq_subscribe_msg_encode(uint8_t* bytes, uint8_t* bytes_max, uint64_t message_type, size_t url_length, const uint8_t* url);
const uint8_t* quicrq_subscribe_msg_decode(const uint8_t* bytes, const uint8_t* bytes_max, uint64_t* message_type, size_t* url_length, const uint8_t** url);
size_t quicrq_notify_msg_reserve(size_t url_length);
uint8_t* quicrq_notify_msg_encode(uint8_t* bytes, uint8_t* bytes_max, uint64_t message_type, size_t url_length, const uint8_t* url);
const uint8_t* quicrq_notify_msg_decode(const uint8_t* bytes, const uint8_t* bytes_max, uint64_t* message_type, size_t* url_length, const uint8_t** url);
size_t quicrq_rq_msg_reserve(size_t url_length);
uint8_t* quicrq_rq_msg_encode(uint8_t* bytes, uint8_t* bytes_max, uint64_t message_type, size_t url_length, const uint8_t* url, uint64_t datagram_stream_id);
const uint8_t* quicrq_rq_msg_decode(const uint8_t* bytes, const uint8_t* bytes_max, uint64_t* message_type, size_t* url_length, const uint8_t** url, uint64_t* datagram_stream_id);
size_t quicrq_fin_msg_reserve(uint64_t final_group_id, uint64_t final_object_id);
uint8_t* quicrq_fin_msg_encode(uint8_t* bytes, uint8_t* bytes_max, uint64_t message_type, 
    uint64_t final_group_id, uint64_t final_object_id);
const uint8_t* quicrq_fin_msg_decode(const uint8_t* bytes, const uint8_t* bytes_max,
    uint64_t* message_type, uint64_t* final_group_id, uint64_t* final_object_id);
size_t quicrq_repair_request_reserve(uint64_t repair_group_id, uint64_t repair_object_id, uint64_t repair_offset, int is_last_fragment, size_t repair_length);
uint8_t* quicrq_repair_request_encode(uint8_t* bytes, uint8_t* bytes_max, uint64_t message_type, uint64_t repair_group_id, uint64_t repair_object_id, uint64_t repair_offset, int is_last_fragment, size_t repair_length);
const uint8_t* quicrq_repair_request_decode(const uint8_t* bytes, const uint8_t* bytes_max, uint64_t* message_type, uint64_t* repair_group_id, uint64_t* repair_object_id, uint64_t* repair_offset, int* is_last_fragment, size_t* repair_length);
size_t quicrq_fragment_msg_reserve(uint64_t group_id, uint64_t object_id, 
    uint64_t nb_objects_previous_group,
    uint64_t offset, int is_last_fragment, size_t data_length);
uint8_t* quicrq_fragment_msg_encode(uint8_t* bytes, uint8_t* bytes_max, uint64_t message_type,
    uint64_t group_id, uint64_t object_id, uint64_t nb_objects_previous_group,
    uint64_t offset, int is_last_fragment, uint8_t flags, size_t length, const uint8_t* data);
const uint8_t* quicrq_fragment_msg_decode(const uint8_t* bytes, const uint8_t* bytes_max, uint64_t* message_type,
    uint64_t* group_id, uint64_t* object_id, uint64_t* nb_objects_previous_group,
    uint64_t* offset, int* is_last_fragment, uint8_t* flags, size_t* length, const uint8_t** data);
size_t quicrq_start_point_msg_reserve(uint64_t start_group, uint64_t start_object);
uint8_t* quicrq_start_point_msg_encode(uint8_t* bytes, uint8_t* bytes_max, uint64_t message_type, uint64_t start_group, uint64_t start_object);
const uint8_t* quicrq_start_point_msg_decode(const uint8_t* bytes, const uint8_t* bytes_max, uint64_t* message_type, uint64_t* start_group, uint64_t* start_object);
uint8_t* quicrq_msg_encode(uint8_t* bytes, uint8_t* bytes_max, quicrq_message_t* msg);
const uint8_t* quicrq_msg_decode(const uint8_t* bytes, const uint8_t* bytes_max, quicrq_message_t * msg);

/* Encode and decode the header of datagram packets. */
#define QUICRQ_DATAGRAM_HEADER_MAX 16
uint8_t* quicrq_datagram_header_encode(uint8_t* bytes, uint8_t* bytes_max, uint64_t datagram_stream_id, uint64_t group_id, 
    uint64_t object_id, uint64_t object_offset, uint64_t queue_delay, uint8_t flags, uint64_t nb_objects_previous_group, int is_last_fragment);
const uint8_t* quicrq_datagram_header_decode(const uint8_t* bytes, const uint8_t* bytes_max, uint64_t* datagram_stream_id, uint64_t* group_id,
    uint64_t* object_id, uint64_t* object_offset, uint64_t *queue_delay, uint8_t * flags, uint64_t *nb_objects_previous_group, int * is_last_fragment);
/* Stream header is indentical to repair message */
#define QUICRQ_STREAM_HEADER_MAX 2+1+8+4+2

/* Initialize the tracking of a datagram after sending it in a stream context */
int quicrq_datagram_ack_init(quicrq_stream_ctx_t* stream_ctx, uint64_t group_id, uint64_t object_id,
    uint64_t object_offset, uint64_t nb_objects_previous_group, const uint8_t* data, size_t length,
    uint64_t queue_delay, int is_last_fragment, void** p_created_state, uint64_t current_time);

/* Transmission of out of order datagrams is possible for relays.
 * We use a function pointer to isolate the relay code for the main code,
 * and ensure the quicrq library can be used in clients without
 * loading the relay code.
 */
typedef int (*quicrq_datagram_publisher_fn)(
    quicrq_stream_ctx_t* stream_ctx,
    void* context,
    size_t space,
    int* media_was_sent,
    int* at_least_one_active,
    uint64_t current_time);

/* Media publisher API.
 * This now only an internal API. 
 *
 * Publisher connect a source to a local context by calling `quicrq_publish_source`.
 * This registers an URL for the source, and creates a source entry in the local context.
 *
 * - qr_ctx: QUICR context in which the media is published
 * - url, url_length: URL of the media fragment
 * - media_publisher_subscribe_fn: callback function for subscribing a new consumer to the media source.
 * - media_publisher_fn: callback function for processing media arrival
 * - media_ctx: media context managed by the publisher, specific to that URL.
 *
 * When a subscribe request arrives, the stack looks for a media source, which could be
 * an actual source, or the cached version of the media published by another node
 * for that URL. (In relays and origin servers, a new cache entry is automatically
 * created upon the request to an URL.) Once the stack has identified the source
 * context, it will make a first call to the "subscribe" function, which will
 * return a "media context" specific to that source and that subscription.
 *
 * After that, the stack will try to send the media as a series of objects, each
 * composed of a series of fragments. The data is obtained by a series of calls
 * to the "publisher" function, with the following parameters:
 *
 * - action: set to get data for retrieving data, or close to indicate end of
 *   the transmission. After a call to close, the media context can be freed.
 * - media_ctx: as produced by the call to the subscribe function.
 * - data: either NULL, or a pointer to the memory location where data shall
 *   be copied. (See below for the logic of calling the function twice)
 * - data_max_size: the space available at the memory location.
 * - &data_length: the data available to fill that space.
 * - &is_last_fragment: whether this is the last fragment in a object
 * - &is_media_finished: whether there is no more data to send.
 * - current_time: time, in microseconds. (May be virtual time during simulations
 *   and tests.)
 *
 * The stack will make two calls to fill a packet: a first call with "data" set
 * to NULL to learn the number of bytes available, and the value of "is_last_fragment"
 * and "is_media_finished", and a second call to actually request the data. It is
 * essential that data_length, is_last_fragment and is_media_finished are set to
 * the same value in both calls.
 *
 * The media is sent as a series of objects. The stack inserts a small header in
 * front of each fragment to specify the object number, the offset in the object,
 * and whether this is the last fragment. This is used by the reassembly
 * processes (see quicrq_reassembly.h). Intermediate relay may wait until the
 * last fragment is received to forward data belonging to a object.
 */

typedef enum {
    quicrq_media_source_get_data = 0,
    quicrq_media_source_skip_object,
    quicrq_media_source_close
} quicrq_media_source_action_enum;

typedef void* (*quicrq_media_publisher_subscribe_fn)(void* pub_ctx, quicrq_stream_ctx_t* stream_ctx);
typedef int (*quicrq_media_publisher_fn)(
    quicrq_media_source_action_enum action,
    void* media_ctx,
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
typedef void (*quicrq_media_publisher_delete_fn)(void* pub_ctx);

typedef struct st_quicrq_media_source_ctx_t quicrq_media_source_ctx_t;
quicrq_media_source_ctx_t* quicrq_publish_source(quicrq_ctx_t* qr_ctx, const uint8_t* url, size_t url_length,
    void* pub_ctx, quicrq_media_publisher_subscribe_fn subscribe_fn,
    quicrq_media_publisher_fn getdata_fn, quicrq_media_publisher_delete_fn delete_fn);
void quicrq_delete_source(quicrq_media_source_ctx_t* srce_ctx, quicrq_ctx_t* qr_ctx);
void quicrq_source_wakeup(quicrq_media_source_ctx_t* srce_ctx);


 /* Quicrq per media object source context.
  */

struct st_quicrq_media_object_source_ctx_t {
    quicrq_ctx_t* qr_ctx;
    struct st_quicrq_media_object_source_ctx_t* previous_in_qr_ctx;
    struct st_quicrq_media_object_source_ctx_t* next_in_qr_ctx;
    quicrq_media_source_ctx_t* media_source_ctx;
    quicrq_media_object_source_properties_t properties;
    uint64_t start_group_id;
    uint64_t start_object_id;
    uint64_t next_group_id;
    uint64_t next_object_id;
    picosplay_tree_t object_source_tree;
    int is_finished;
};

/* Quicrq per media source context.
 */

struct st_quicrq_media_source_ctx_t {
    struct st_quicrq_media_source_ctx_t* next_source;
    struct st_quicrq_media_source_ctx_t* previous_source;
    struct st_quicrq_stream_ctx_t* first_stream;
    struct st_quicrq_stream_ctx_t* last_stream;
    uint8_t* media_url;
    size_t media_url_length;
    void* pub_ctx;
    quicrq_media_publisher_subscribe_fn subscribe_fn;
    quicrq_media_publisher_fn getdata_fn;
    quicrq_datagram_publisher_fn get_datagram_fn;
    quicrq_media_publisher_delete_fn delete_fn;
};

quicrq_media_source_ctx_t* quicrq_find_local_media_source(quicrq_ctx_t* qr_ctx, const uint8_t* url, const size_t url_length);
int quicrq_subscribe_local_media(quicrq_stream_ctx_t* stream_ctx, const uint8_t* url, const size_t url_length);
void quicrq_unsubscribe_local_media(quicrq_stream_ctx_t* stream_ctx);
void quicrq_wakeup_media_stream(quicrq_stream_ctx_t* stream_ctx);

int quicrq_media_object_publisher(
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

void* quicrq_media_object_publisher_subscribe(void* pub_ctx, quicrq_stream_ctx_t* stream_ctx);

/* Quic media consumer. Old definition, moved to internal only.
 * 
 * The application sets a "media consumer function" and a "media consumer context" for
 * the media stream. On the client side, this is done by a call to "quicrq_cnx_subscribe_media"
 * which will trigger the opening of the media stream through the protocol.
 *
 * For client published streams, the client uses "quicrq_cnx_post_media"
 * to start the media stream. The server will receive an initial command
 * containing the media URL, and use
 */

typedef int (*quicrq_media_consumer_fn)(
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
    size_t data_length);

int quicrq_cnx_subscribe_media(quicrq_cnx_ctx_t* cnx_ctx,
    const uint8_t* url, size_t url_length, int use_datagrams,
    quicrq_media_consumer_fn media_consumer_fn, void* media_ctx);

int quicrq_cnx_subscribe_media_ex(quicrq_cnx_ctx_t* cnx_ctx, const uint8_t* url, size_t url_length,
    int use_datagrams, quicrq_media_consumer_fn media_consumer_fn, void* media_ctx, quicrq_stream_ctx_t** p_stream_ctx);


/* Quicrq stream handling.
 * Media stream come in two variants.
 * - server to client stream, that must include the API for sending data from a stream.
 * - client to server stream, that must include the API for receiving data.
 * Media can be sent in two modes, stream or datagram. In stream mode, 
 * the server just posts the media content on the stream. In datagram
 * mode, the server posts the content as a set of datagrams. The server
 * may also post a set of "datagram repair" corrections, when datagrams
 * are deemed missing.
 */
 /* Quic media consumer */
typedef enum {
    quicrq_sending_ready = 0,
    quicrq_sending_stream,
    quicrq_sending_initial,
    quicrq_sending_repair,
    quicrq_sending_offset,
    quicrq_sending_start_point,
    quicrq_sending_fin,
    quicrq_sending_subscribe,
    quicrq_waiting_notify,
    quicrq_sending_notify,
    quicrq_notify_ready,
    quicrq_sending_no_more
} quicrq_stream_sending_state_enum;

typedef enum {
    quicrq_receive_initial = 0,
    quicrq_receive_stream,
    quicrq_receive_confirmation,
    quicrq_receive_fragment,
    quicrq_receive_notify,
    quicrq_receive_done
}  quicrq_stream_receive_state_enum;

typedef struct st_quicrq_datagram_ack_state_t {
    picosplay_node_t datagram_ack_node;
    uint64_t group_id;
    uint64_t object_id;
    uint64_t object_offset;
    uint64_t nb_objects_previous_group;
    uint64_t queue_delay;
    uint8_t flags;
    int is_last_fragment;
    size_t length;
    int is_acked;
    int nack_received;
    /* Handling of extra repeat, i.e., poor man's FEC.
     * Presence of extra data indicates an extra repeat is scheduled. 
     * Length of extra_data is always equal to length of fragment.
     */
    struct st_quicrq_datagram_ack_state_t* extra_previous;
    struct st_quicrq_datagram_ack_state_t* extra_next;
    uint64_t extra_repeat_time;
    uint8_t* extra_data;
    int is_extra_queued;
    /* Start time is the time of the first transmission at this node */
    uint64_t start_time;
    /* last sent time might help differentiating NACK of old vs. NACK of last */
    uint64_t last_sent_time;
} quicrq_datagram_ack_state_t;

typedef struct st_quicrq_notify_url_t {
    struct st_quicrq_notify_url_t* next_notify_url;
    size_t url_len;
    uint8_t* url;
} quicrq_notify_url_t;

struct st_quicrq_stream_ctx_t {
    struct st_quicrq_stream_ctx_t* next_stream;
    struct st_quicrq_stream_ctx_t* previous_stream;
    struct st_quicrq_cnx_ctx_t* cnx_ctx;
    quicrq_media_source_ctx_t* media_source;
    struct st_quicrq_stream_ctx_t* next_stream_for_source;
    struct st_quicrq_stream_ctx_t* previous_stream_for_source;
    /* queue of datagrams that qualify for extra transmission */
    struct st_quicrq_datagram_ack_state_t* extra_first;
    struct st_quicrq_datagram_ack_state_t* extra_last;
    /* stream identifier */
    uint64_t stream_id;
    uint64_t datagram_stream_id;
    uint64_t next_group_id;
    uint64_t next_object_id;
    uint64_t next_object_offset;
    uint64_t start_group_id;
    uint64_t start_object_id;
    uint64_t final_group_id;
    uint64_t final_object_id;
    /* Control of datagrams sent for that media
     * We only keep track of fragments that are above the horizon.
     * The one below horizon are already acked, or otherwise forgotten.
     */
    uint64_t horizon_group_id;
    uint64_t horizon_object_id;
    uint64_t horizon_offset;
    int horizon_is_last_fragment;
    int nb_horizon_events;
    int nb_horizon_acks;
    int nb_extra_sent;
    int nb_fragment_lost;
    picosplay_tree_t datagram_ack_tree;
    /* For notification streams, URL and notification queue */
    uint8_t* subscribe_prefix;
    size_t subscribe_prefix_length;
    quicrq_notify_url_t* first_notify_url;
    quicrq_media_notify_fn media_notify_fn;
    void* notify_ctx;
    /* Stream state */
    quicrq_stream_sending_state_enum send_state;
    quicrq_stream_receive_state_enum receive_state;
    unsigned int is_client : 1;
    unsigned int is_sender : 1;
    /* For the sender, receiver finished happens if the client closes the control stream.
     * In that case, the server should close the stream and mark itself finished.
     * For the receiver, the transfer finishes if everything was received. In that
     * case, the receiver shall close the control stream. If the sender closes the
     * control stream before that, we have an abnormal close.
     */
    unsigned int is_peer_finished : 1;
    unsigned int is_local_finished : 1;
    unsigned int is_receive_complete: 1;
    unsigned int is_datagram : 1;
    unsigned int is_active_datagram : 1;
    unsigned int is_start_object_id_sent : 1;
    unsigned int is_final_object_id_sent : 1;

    size_t bytes_sent;
    size_t bytes_received;

    quicrq_message_buffer_t message_sent;
    quicrq_message_buffer_t message_receive;

    quicrq_media_consumer_fn consumer_fn; /* Callback function for media data arrival  */
    quicrq_media_publisher_fn publisher_fn; /* Data providing function for source */
    quicrq_datagram_publisher_fn get_datagram_fn; /* Out of order publishing of datagrams */
    void* media_ctx; /* Callback argument for receiving or sending data */
};

int quicrq_set_media_stream_ctx(quicrq_stream_ctx_t* stream_ctx, quicrq_media_consumer_fn media_fn, void* media_ctx);

quicrq_media_source_ctx_t* quicrq_publish_datagram_source(quicrq_ctx_t* qr_ctx, const uint8_t* url, size_t url_length,
    void* pub_ctx, quicrq_media_publisher_subscribe_fn subscribe_fn,
    quicrq_media_publisher_fn getdata_fn, quicrq_datagram_publisher_fn get_datagram_fn, quicrq_media_publisher_delete_fn delete_fn);

typedef struct st_quicrq_cnx_congestion_state_t {
    int has_backlog; /* Indicates whether at least on flow is congested. */
    int is_congested;
    uint8_t max_flags; /* largest flag value across streams, used in congestion control */
    uint8_t priority_threshold; /* Indicates the highest priority level that may be dropped. */
    uint8_t old_priority_threshold; /* Threshold at beginning of epoch. */
    uint64_t congestion_check_time;
} quicrq_cnx_congestion_state_t;

/* Quicrq per connection context */
struct st_quicrq_cnx_ctx_t {
    struct st_quicrq_cnx_ctx_t* next_cnx;
    struct st_quicrq_cnx_ctx_t* previous_cnx;
    struct st_quicrq_ctx_t* qr_ctx;

    char* sni;
    struct sockaddr_storage addr;
    picoquic_cnx_t* cnx;
    int is_server;
    quicrq_cnx_congestion_state_t congestion;

    uint64_t next_datagram_stream_id; /* only used for receiving */
    uint64_t next_abandon_datagram_id; /* used to test whether unexpected datagrams are OK */
    struct st_quicrq_stream_ctx_t* first_stream;
    struct st_quicrq_stream_ctx_t* last_stream;
};

/* Prototype function for managing the cache of relays.
 * Using a function pointer allows pure clients to operate without loading
 * the relay functionality.
 * Function returns the next time at which management action is needed.
 */
typedef uint64_t (*quicrq_manage_relay_cache_fn)(quicrq_ctx_t* qr_ctx, uint64_t current_time);

/* Management of notifications
 */

int quicrq_notify_url_to_stream(quicrq_stream_ctx_t* stream_ctx, const uint8_t* url, size_t url_length);
int quicrq_notify_url_to_all(quicrq_ctx_t * qr_ctx, const uint8_t* url, size_t url_length);

/* Prototype function for managing cache at relay. */
typedef enum {
    quicrq_subscribe_action_subscribe,
    quicrq_subscribe_action_unsubscribe
} quicrq_subscribe_action_enum;

typedef void (*quicrq_manage_relay_subscribe_fn)(quicrq_ctx_t* qr_ctx, quicrq_subscribe_action_enum action, const uint8_t* url, size_t url_length);

/* Quicrq context */
struct st_quicrq_ctx_t {
    picoquic_quic_t* quic; /* The quic context for the Quicrq service */
    /* Local media sources */
    quicrq_media_source_ctx_t* first_source;
    quicrq_media_source_ctx_t* last_source;
    /* local media object sources */
    struct st_quicrq_media_object_source_ctx_t* first_object_source;
    struct st_quicrq_media_object_source_ctx_t* last_object_source;
    /* Relay context, if is acting as relay or origin */
    struct st_quicrq_relay_context_t* relay_ctx;
    /* Default publisher function, used for example by relays */
    quicrq_default_source_fn default_source_fn;
    void* default_source_ctx;
    /* Local media receiver function */
    quicrq_media_consumer_init_fn consumer_media_init_fn;
    /* List of connections */
    struct st_quicrq_cnx_ctx_t* first_cnx; /* First in double linked list of open connections in this context */
    struct st_quicrq_cnx_ctx_t* last_cnx; /* last in list of open connections in this context */
    /* Cache management:
     * cache_duration_max in micros seconds, or zero if no cache management required
     * cache will be checked at once every cache_duration_max/2, as controlled
     * by cache_check_next_time.
     * When checking cache, the function manage_relay_cache_fn is called if the
     * relay function is enabled.
     */
    int is_cache_closing_needed;
    uint64_t cache_duration_max;
    uint64_t cache_check_next_time;
    quicrq_manage_relay_cache_fn manage_relay_cache_fn;
    quicrq_manage_relay_subscribe_fn manage_relay_subscribe_fn;
    /* Extra repeat option */
    int extra_repeat_on_nack : 1;
    int extra_repeat_after_received_delayed : 1;
    uint64_t extra_repeat_delay;
    /* Control whether to enable congestion control -- mostly for testability */
    unsigned int do_congestion_control : 1;
};

quicrq_stream_ctx_t* quicrq_find_or_create_stream(
    uint64_t stream_id,
    quicrq_cnx_ctx_t* cnx_ctx,
    int should_create);

quicrq_stream_ctx_t* quicrq_create_stream_context(quicrq_cnx_ctx_t* cnx_ctx, uint64_t stream_id);

void quicrq_delete_stream_ctx(quicrq_cnx_ctx_t* cnx_ctx, quicrq_stream_ctx_t* stream_ctx);

/* Encode and decode the object header */
const uint8_t* quicr_decode_object_header(const uint8_t* fh, const uint8_t* fh_max, quicrq_media_object_header_t* hdr);
uint8_t* quicr_encode_object_header(uint8_t* fh, const uint8_t* fh_max, const quicrq_media_object_header_t* hdr);

/* Process a receive POST command */
int quicrq_cnx_accept_media(quicrq_stream_ctx_t* stream_ctx, const uint8_t* url, size_t url_length,
    int use_datagrams);

/*  Process a received ACCEPT response */
int quicrq_cnx_post_accepted(quicrq_stream_ctx_t* stream_ctx, unsigned int use_datagrams, uint64_t datagram_stream_id);

/* Handle closure of stream after receiving the last bit of data */
int quicrq_cnx_handle_consumer_finished(quicrq_stream_ctx_t* stream_ctx, int is_final, int is_datagram, int ret);

void quicrq_cnx_abandon_stream_id(quicrq_cnx_ctx_t* cnx_ctx, uint64_t stream_id);

void quicrq_cnx_abandon_stream(quicrq_stream_ctx_t* stream_ctx);

/* Media bridge defintions, useful for tests */
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
    size_t data_length);
/* For logging.. */
const char* quicrq_uint8_t_to_text(const uint8_t* u, size_t length, char* buffer, size_t buffer_length);
void quicrq_log_message(quicrq_cnx_ctx_t* cnx_ctx, const char* fmt, ...);

/* Evaluation of congestion state */
int quicrq_congestion_check_per_cnx(quicrq_cnx_ctx_t* cnx_ctx, uint8_t flags, int has_backlog, uint64_t current_time);

#ifdef __cplusplus
}
#endif

#endif /* quicrq_internal_H */
