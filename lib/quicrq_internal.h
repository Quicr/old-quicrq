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


/* Quicrq stream handling */
struct st_quicrq_stream_ctx_t {
    uint64_t stream_id;
    struct st_quicrq_stream_ctx_t* next_stream;
    struct st_quicrq_stream_ctx_t* previous_stream;
    struct st_quicrq_cnx_ctx_t* cnx_ctx;
    size_t bytes_sent;
    size_t bytes_received;
    uint16_t length_received;

    unsigned int client_mode : 1;
};

/* Quicrq per connection context */
struct st_quicrq_cnx_ctx_t {
    struct st_quicrq_cnx_ctx_t* next_cnx;
    struct st_quicrq_cnx_ctx_t* previous_cnx;
    struct st_quicrq_ctx_t* quicrq_ctx;

    char* sni;
    struct sockaddr_storage addr;
    picoquic_cnx_t* cnx;
    int is_server;

    uint64_t next_available_stream_id; /* starts with stream 0 on client */
    struct st_quicrq_stream_ctx_t* first_stream;
    struct st_quicrq_stream_ctx_t* last_stream;
};

/* Quicrq context */
struct st_quicrq_ctx_t {
    picoquic_quic_t* quic; /* The quic context for the Quicrq service */
    /* Todo: message passing and synchronization */
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

int quicrq_callback(picoquic_cnx_t* cnx,
    uint64_t stream_id, uint8_t* bytes, size_t length,
    picoquic_call_back_event_t fin_or_event, void* callback_ctx, void* v_stream_ctx);

int quicrq_callback_data(picoquic_cnx_t* cnx, quicrq_stream_ctx_t* stream_ctx, uint64_t stream_id, uint8_t* bytes,
    size_t length, picoquic_call_back_event_t fin_or_event, quicrq_cnx_ctx_t* cnx_ctx);

int quicrq_callback_prepare_to_send(picoquic_cnx_t* cnx, uint64_t stream_id, quicrq_stream_ctx_t* stream_ctx,
    void* bytes, size_t length, quicrq_cnx_ctx_t* cnx_ctx);

/* Set the parameters to the preferred Quicrq values for the client */
void quicrq_set_tp(picoquic_cnx_t* cnx);
/* Set default transport parameters to adequate value for quicrq server. */
int quicrq_set_default_tp(quicrq_ctx_t* quicrq_ctx);

/* Encode and decode the frame header */
const uint8_t* quicr_decode_frame_header(const uint8_t* fh, const uint8_t* fh_max, quicrq_media_frame_header_t* hdr);
uint8_t* quicr_encode_frame_header(uint8_t* fh, const uint8_t* fh_max, const quicrq_media_frame_header_t* hdr);

#ifdef __cplusplus
}
#endif

#endif /* quicrq_client_internal_H */