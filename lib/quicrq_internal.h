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

#ifndef quicrq_client_internal_H
#define quicrq_client_internal_H

#include "picoquic.h"
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

/* The protocol used for our tests defines a set of actions:
 * - Open Stream: request to open a stream, defined by URL of media segment. Content will be sent as a stream of bytes.
 * - Open datagram: same as open stream, but specifying opening as a "datagram" stream and providing the ID of that stream.
 *   Content will be sent as a stream of datagrams, each specifying an offset and a set of bytes.
 * - Fin Datagram: when the media segment has been sent as a set of datagrams, provides the final offset.
 * - Request repair: when a stream is opened as datagram, some datagrams may be lost. The receiver may request data at offset and length.
 * - Repair: 1 byte code, followed by content of a datagram
 */
#define QUICRQ_ACTION_OPEN_STREAM 1
#define QUICRQ_ACTION_OPEN_DATAGRAM 2
#define QUICRQ_ACTION_FIN_DATAGRAM 3
#define QUICRQ_ACTION_REQUEST_REPAIR 4
#define QUICRQ_ACTION_REPAIR 5
#define QUICRQ_ACTION_POST 6
#define QUICRQ_ACTION_ACCEPT 7

/* Protocol message.
 * This structure is used when decoding messages
 */
typedef struct st_quicrq_message_t {
    uint64_t message_type;
    size_t url_length;
    const uint8_t* url;
    uint64_t datagram_stream_id;
    uint64_t frame_id;
    uint64_t offset;
    int is_last_segment;
    size_t length;
    const uint8_t* data;
    unsigned int use_datagram;
} quicrq_message_t;

/* Encode and decode protocol messages */
size_t quicrq_rq_msg_reserve(size_t url_length);
uint8_t* quicrq_rq_msg_encode(uint8_t* bytes, uint8_t* bytes_max, uint64_t message_type, size_t url_length, const uint8_t* url, uint64_t datagram_stream_id);
const uint8_t* quicrq_rq_msg_decode(const uint8_t* bytes, const uint8_t* bytes_max, uint64_t* message_type, size_t* url_length, const uint8_t** url, uint64_t* datagram_stream_id);
size_t quicrq_fin_msg_reserve(uint64_t final_offset);
uint8_t* quicrq_fin_msg_encode(uint8_t* bytes, uint8_t* bytes_max, uint64_t message_type, uint64_t final_frame_id);
const uint8_t* quicrq_fin_msg_decode(const uint8_t* bytes, const uint8_t* bytes_max, uint64_t* message_type, uint64_t* final_frame_id);
size_t quicrq_repair_request_reserve(uint64_t repair_frame_id, uint64_t repair_offset, int is_last_segment, size_t repair_length);
uint8_t* quicrq_repair_request_encode(uint8_t* bytes, uint8_t* bytes_max, uint64_t message_type, uint64_t repair_frame_id, uint64_t repair_offset, int is_last_segment, size_t repair_length);
const uint8_t* quicrq_repair_request_decode(const uint8_t* bytes, const uint8_t* bytes_max, uint64_t* message_type, uint64_t* repair_frame_id, uint64_t* repair_offset, int* is_last_segment, size_t* repair_length);
size_t quicrq_repair_msg_reserve(uint64_t repair_frame_id, uint64_t repair_offset, int is_last_segment, size_t repair_length);
uint8_t* quicrq_repair_msg_encode(uint8_t* bytes, uint8_t* bytes_max, uint64_t message_type, uint64_t repair_frame_id, uint64_t repair_offset, int is_last_segment, size_t repair_length, const uint8_t* repair_data);
const uint8_t* quicrq_repair_msg_decode(const uint8_t* bytes, const uint8_t* bytes_max, uint64_t* message_type, uint64_t* repair_frame_id, uint64_t* repair_offset, int* is_last_segment, size_t* repair_length, const uint8_t** repair_data);
uint8_t* quicrq_msg_encode(uint8_t* bytes, uint8_t* bytes_max, quicrq_message_t* msg);
const uint8_t* quicrq_msg_decode(const uint8_t* bytes, const uint8_t* bytes_max, quicrq_message_t * msg);

/* Encode and decode the header of datagram packets. */
#define QUICRQ_DATAGRAM_HEADER_MAX 16
uint8_t* quicrq_datagram_header_encode(uint8_t* bytes, uint8_t* bytes_max, uint64_t datagram_stream_id, uint64_t frame_id, uint64_t frame_offset, int is_last_segment);
const uint8_t* quicrq_datagram_header_decode(const uint8_t* bytes, const uint8_t* bytes_max, uint64_t* datagram_stream_id, 
    uint64_t* frame_id, uint64_t* frame_offset, int * is_last_segment);
/* Stream header is indentical to repair message */
#define QUICRQ_STREAM_HEADER_MAX 2+1+8+4+2

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
};

quicrq_media_source_ctx_t* quicrq_find_local_media_source(quicrq_ctx_t* qr_ctx, const uint8_t* url, const size_t url_length);
int quicrq_subscribe_local_media(quicrq_stream_ctx_t* stream_ctx, const uint8_t* url, const size_t url_length);
void quicrq_unsubscribe_local_media(quicrq_stream_ctx_t* stream_ctx);
void quicrq_wakeup_media_stream(quicrq_stream_ctx_t* stream_ctx);

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
    quicrq_sending_fin,
    quicrq_sending_no_more
} quicrq_stream_sending_state_enum;

typedef enum {
    quicrq_receive_initial = 0,
    quicrq_receive_stream,
    quicrq_receive_confirmation,
    quicrq_receive_repair,
    quicrq_receive_done
}  quicrq_stream_receive_state_enum;


typedef struct st_quicrq_datagram_queued_repair_t {
    struct st_quicrq_datagram_queued_repair_t* next_repair;
    struct st_quicrq_datagram_queued_repair_t* previous_repair;
    uint8_t* datagram;
    uint64_t frame_id;
    uint64_t frame_offset;
    int is_last_segment;
    size_t length;
} quicrq_datagram_queued_repair_t;

struct st_quicrq_stream_ctx_t {
    struct st_quicrq_stream_ctx_t* next_stream;
    struct st_quicrq_stream_ctx_t* previous_stream;
    struct st_quicrq_cnx_ctx_t* cnx_ctx;
    quicrq_media_source_ctx_t* media_source;
    struct st_quicrq_stream_ctx_t* next_stream_for_source;
    struct st_quicrq_stream_ctx_t* previous_stream_for_source;

    quicrq_datagram_queued_repair_t* datagram_repair_first;
    quicrq_datagram_queued_repair_t* datagram_repair_last;

    uint64_t stream_id;
    uint64_t datagram_stream_id;
    uint64_t next_frame_id;
    uint64_t next_frame_offset;
    uint64_t final_frame_id;

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
    unsigned int is_final_frame_id_sent : 1;

    size_t bytes_sent;
    size_t bytes_received;

    quicrq_message_buffer_t message_sent;
    quicrq_message_buffer_t message_receive;

    quicrq_media_consumer_fn consumer_fn; /* Callback function for media data arrival  */
    quicrq_media_publisher_fn publisher_fn; /* Data providing function for source */
    void* media_ctx; /* Callback argument for receiving or sending data */
};

/* Quicrq per connection context */
struct st_quicrq_cnx_ctx_t {
    struct st_quicrq_cnx_ctx_t* next_cnx;
    struct st_quicrq_cnx_ctx_t* previous_cnx;
    struct st_quicrq_ctx_t* qr_ctx;

    char* sni;
    struct sockaddr_storage addr;
    picoquic_cnx_t* cnx;
    int is_server;

    uint64_t next_datagram_stream_id; /* only used for receiving */
    uint64_t next_abandon_datagram_id; /* used to test whether unexpected datagrams are OK */
    struct st_quicrq_stream_ctx_t* first_stream;
    struct st_quicrq_stream_ctx_t* last_stream;
};

/* Quicrq context */
struct st_quicrq_ctx_t {
    picoquic_quic_t* quic; /* The quic context for the Quicrq service */
    /* Local media sources */
    quicrq_media_source_ctx_t* first_source;
    quicrq_media_source_ctx_t* last_source;
    /* Default publisher function, used for example by relays */
    quicrq_default_source_fn default_source_fn;
    void* default_source_ctx;
    /* Local media receiver function */
    quicrq_media_consumer_init_fn consumer_media_init_fn;
    /* Todo: sockets, etc */
    struct st_quicrq_cnx_ctx_t* first_cnx; /* First in double linked list of open connections in this context */
    struct st_quicrq_cnx_ctx_t* last_cnx; /* last in list of open connections in this context */
};

quicrq_stream_ctx_t* quicrq_find_or_create_stream(
    uint64_t stream_id,
    quicrq_cnx_ctx_t* cnx_ctx,
    int should_create);

quicrq_stream_ctx_t* quicrq_create_stream_context(quicrq_cnx_ctx_t* cnx_ctx, uint64_t stream_id);

void quicrq_delete_stream_ctx(quicrq_cnx_ctx_t* cnx_ctx, quicrq_stream_ctx_t* stream_ctx);

#if 0
/* TODO: actually set these paramters... */
/* Set the parameters to the preferred Quicrq values for the client */
void quicrq_set_tp(picoquic_cnx_t* cnx);
/* Set default transport parameters to adequate value for quicrq server. */
int quicrq_set_default_tp(quicrq_ctx_t* quicrq_ctx);
#endif

/* Encode and decode the frame header */
const uint8_t* quicr_decode_frame_header(const uint8_t* fh, const uint8_t* fh_max, quicrq_media_frame_header_t* hdr);
uint8_t* quicr_encode_frame_header(uint8_t* fh, const uint8_t* fh_max, const quicrq_media_frame_header_t* hdr);

/* Process a receive POST command */
int quicrq_cnx_accept_media(quicrq_stream_ctx_t* stream_ctx, const uint8_t* url, size_t url_length,
    int use_datagrams);

/*  Process a received ACCEPT response */
int quicrq_cnx_post_accepted(quicrq_stream_ctx_t* stream_ctx, unsigned int use_datagrams, uint64_t datagram_stream_id);

/* Handle closure of stream after receiving the last bit of data */
int quicrq_cnx_handle_consumer_finished(quicrq_stream_ctx_t* stream_ctx, int is_final, int is_datagram, int ret);

void quicrq_cnx_abandon_stream_id(quicrq_cnx_ctx_t* cnx_ctx, uint64_t stream_id);

void quicrq_cnx_abandon_stream(quicrq_stream_ctx_t* stream_ctx);

/* For logging.. */
const char* quicrq_uint8_t_to_text(const uint8_t* u, size_t length, char* buffer, size_t buffer_length);

#ifdef __cplusplus
}
#endif

#endif /* quicrq_client_internal_H */