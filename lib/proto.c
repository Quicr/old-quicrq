/* Coding and decoding of quicrq API and messages
*/

#include <stdlib.h>
#include <string.h>
#include "quicrq.h"
#include "quicrq_internal.h"
#include "quicrq_fragment.h"
#include "picoquic_utils.h"

/* The protocol defines a set of actions, identified by a code. For each action
 * we get a specific encoding and decoding function. We also use a generic decoding
 * structure.
 */

/* Media subscribe message and media notify response.
 * The subscribe message creates a subscription context, asking relay or
 * origin to notify the client when matching URL become available. The response
 * message sent on the same stream notifies the client that a new URL is
 * available.
 * 
 *  quicrq_subscribe_message {
 *     message_type(i),
 *     url_length(i),
 *     url(...)
 *  }
 * 
 *  quicrq_notify_message {
 *     message_type(i),
 *     url_length(i),
 *     url(...)
 *  }
 */

size_t quicrq_subscribe_msg_reserve(size_t url_length)
{
    return 8 + 2 + url_length;
}

uint8_t* quicrq_subscribe_msg_encode(uint8_t* bytes, uint8_t* bytes_max, uint64_t message_type, size_t url_length, const uint8_t* url)
{
    if ((bytes = picoquic_frames_varint_encode(bytes, bytes_max, message_type)) != NULL){
        bytes = picoquic_frames_length_data_encode(bytes, bytes_max, url_length, url);
    }
    return bytes;
}

const uint8_t* quicrq_subscribe_msg_decode(const uint8_t* bytes, const uint8_t* bytes_max, uint64_t* message_type, size_t* url_length, const uint8_t** url)
{
    *url = NULL;
    *url_length = 0;
    if ((bytes = picoquic_frames_varint_decode(bytes, bytes_max, message_type)) != NULL &&
        (bytes = picoquic_frames_varlen_decode(bytes, bytes_max, url_length)) != NULL) {
        *url = bytes;
        bytes = picoquic_frames_fixed_skip(bytes, bytes_max, *url_length);
    }
    return bytes;
}

size_t quicrq_notify_msg_reserve(size_t url_length)
{
    return 8 + 2 + url_length;
}

uint8_t* quicrq_notify_msg_encode(uint8_t* bytes, uint8_t* bytes_max, uint64_t message_type, size_t url_length, const uint8_t* url)
{
    if ((bytes = picoquic_frames_varint_encode(bytes, bytes_max, message_type)) != NULL) {
        bytes = picoquic_frames_length_data_encode(bytes, bytes_max, url_length, url);
    }
    return bytes;
}

const uint8_t* quicrq_notify_msg_decode(const uint8_t* bytes, const uint8_t* bytes_max, uint64_t* message_type, size_t* url_length, const uint8_t** url)
{
    *url = NULL;
    *url_length = 0;
    if ((bytes = picoquic_frames_varint_decode(bytes, bytes_max, message_type)) != NULL &&
        (bytes = picoquic_frames_varlen_decode(bytes, bytes_max, url_length)) != NULL) {
        *url = bytes;
        bytes = picoquic_frames_fixed_skip(bytes, bytes_max, *url_length);
    }
    return bytes;
}

/* Media request message. 
 * 
 * quicrq_request_message {
 *     message_type(i),
 *     url_length(i),
 *     url(...)
 * }
 * 
 * Datagram variant:
 * 
 * }
 * 
 * Revised version: only on type, REQUEST
 * media-ID always present, may be 0 if single stream mode
 * transport mode is stated explicitly
 * 
 * quicrq_request_message {
 *     message_type(i),
 *     url_length(i),
 *     url(...),
 *     media_id(i),
 *     transport_mode,
 *     intent_mode(i),
 *     [ start_group_id(i),
 *       start_object_id(i),]
 * 
 * 
 * Same encoding and decoding code is used for both.
 * 
 */
size_t quicrq_rq_msg_reserve(size_t url_length, quicrq_subscribe_intent_enum intent_mode)
{
    size_t intent_length = (intent_mode == quicrq_subscribe_intent_start_point) ? 17:1;
    return 8 + 2 + url_length + 8 + 1 + intent_length;
}

uint8_t* quicrq_rq_msg_encode(uint8_t* bytes, uint8_t* bytes_max, uint64_t message_type, size_t url_length, const uint8_t* url,
    uint64_t media_id, quicrq_transport_mode_enum transport_mode, quicrq_subscribe_intent_enum intent_mode,
    uint64_t start_group_id,  uint64_t start_object_id)
{
    if ((bytes = picoquic_frames_varint_encode(bytes, bytes_max, message_type)) != NULL &&
        (bytes = picoquic_frames_length_data_encode(bytes, bytes_max, url_length, url)) != NULL &&
        (bytes = picoquic_frames_varint_encode(bytes, bytes_max, media_id)) != NULL &&
        (bytes = picoquic_frames_varint_encode(bytes, bytes_max, (uint64_t)transport_mode)) != NULL &&
        (bytes = picoquic_frames_varint_encode(bytes, bytes_max, (uint64_t)intent_mode)) != NULL){
        if (intent_mode == quicrq_subscribe_intent_start_point){
            if ((bytes = picoquic_frames_varint_encode(bytes, bytes_max, (uint64_t)start_group_id)) != NULL) {
                bytes = picoquic_frames_varint_encode(bytes, bytes_max, (uint64_t)start_object_id);
            }
        }
    }
    return bytes;
}

const uint8_t* quicrq_rq_msg_decode(const uint8_t* bytes, const uint8_t* bytes_max, uint64_t * message_type, size_t * url_length, const uint8_t** url,
    uint64_t *media_id, quicrq_transport_mode_enum* transport_mode, quicrq_subscribe_intent_enum* intent_mode,
    uint64_t *start_group_id, uint64_t *start_object_id)
{
    uint64_t intent_64 = 0;
    uint64_t t_mode_64 = 0;
    *media_id = 0;
    *url = NULL;
    *url_length = 0;
    *transport_mode = 0;
    *intent_mode = 0;
    *start_group_id = 0;
    *start_object_id = 0;

    if ((bytes = picoquic_frames_varint_decode(bytes, bytes_max, message_type)) != NULL &&
        (bytes = picoquic_frames_varlen_decode(bytes, bytes_max, url_length)) != NULL){
        *url = bytes;
        if ((bytes = picoquic_frames_fixed_skip(bytes, bytes_max, *url_length)) != NULL &&
            (bytes = picoquic_frames_varint_decode(bytes, bytes_max, media_id)) != NULL &&
            (bytes = picoquic_frames_varint_decode(bytes, bytes_max, &t_mode_64)) != NULL &&
            (bytes = picoquic_frames_varint_decode(bytes, bytes_max, &intent_64)) != NULL) {
            if (intent_64 > quicrq_subscribe_intent_start_point ||
                t_mode_64 >= quicrq_transport_mode_max) {
                bytes = NULL;
            }
            else {
                *transport_mode = (quicrq_transport_mode_enum)t_mode_64;
                *intent_mode = (quicrq_subscribe_intent_enum)intent_64;

                if (*intent_mode == quicrq_subscribe_intent_start_point) {
                    if ((bytes = picoquic_frames_varint_decode(bytes, bytes_max, start_group_id)) != NULL) {
                        bytes = picoquic_frames_varint_decode(bytes, bytes_max, start_object_id);
                    }
                }
            }
        }
    }
    return bytes;
}
/* Encoding or decoding the fin of datagram stream message
 * 
 * quicrq_fin_message {
 *     message_type(i),
 *     final_group_id(i),
 *     final_object_id(i)
 * }
 */

size_t quicrq_fin_msg_reserve(uint64_t final_group_id, uint64_t final_object_id)
{
    size_t len = 1 +
        picoquic_frames_varint_encode_length(final_group_id) + 
        picoquic_frames_varint_encode_length(final_object_id);
    return len;
}

uint8_t* quicrq_fin_msg_encode(uint8_t* bytes, uint8_t* bytes_max, uint64_t message_type,
    uint64_t final_group_id, uint64_t final_object_id)
{
    if ((bytes = picoquic_frames_varint_encode(bytes, bytes_max, message_type)) != NULL &&
        (bytes = picoquic_frames_varint_encode(bytes, bytes_max, final_group_id)) != NULL) {
        bytes = picoquic_frames_varint_encode(bytes, bytes_max, final_object_id);
    }
    return bytes;
}

const uint8_t* quicrq_fin_msg_decode(const uint8_t* bytes, const uint8_t* bytes_max, uint64_t* message_type,
    uint64_t* final_group_id, uint64_t* final_object_id)
{
    *final_group_id = 0;
    *final_object_id = 0;
    if ((bytes = picoquic_frames_varint_decode(bytes, bytes_max, message_type)) != NULL &&
        (bytes = picoquic_frames_varint_decode(bytes, bytes_max, final_group_id)) != NULL) {
        bytes = picoquic_frames_varint_decode(bytes, bytes_max, final_object_id);
    }
    return bytes;
}

/* Encoding or decoding the fragment message
 *
 * quicrq_fragment_message {
 *     message_type(i),
 *     group_id(i),
 *     object_id(i),
 *     fragment_offset(i),
 *     object_length(i),
 *     flags (8),
 *     [nb_objects_previous_group(i)],
 *     fragment_length(i),
 *     data(...)
 * }
 * 
 * Calling the encoding function with the "data" parameter set to NULL results in the encoding
 * of the fragment message header, minus the data.
 */

size_t quicrq_fragment_msg_reserve(uint64_t group_id, uint64_t object_id, uint64_t nb_objects_previous_group, uint64_t offset, uint64_t object_length, size_t data_length)
{
    size_t len = 1 +
        picoquic_frames_varint_encode_length(group_id) +
        picoquic_frames_varint_encode_length(object_id) +
        picoquic_frames_varint_encode_length(offset) +
        picoquic_frames_varint_encode_length(object_length) +
        1;
    if (object_id == 0 && offset == 0) {
        len += picoquic_frames_varint_encode_length(nb_objects_previous_group);
    }
    len += picoquic_frames_varint_encode_length(data_length);

    return len;
}

uint8_t* quicrq_fragment_msg_encode(uint8_t* bytes, uint8_t* bytes_max, uint64_t message_type,
    uint64_t group_id, uint64_t object_id, uint64_t nb_objects_previous_group,
    uint64_t offset, uint64_t object_length, uint8_t flags, size_t length, const uint8_t * data)
{
    if ((bytes = picoquic_frames_varint_encode(bytes, bytes_max, message_type)) != NULL &&
        (bytes = picoquic_frames_varint_encode(bytes, bytes_max, group_id)) != NULL &&
        (bytes = picoquic_frames_varint_encode(bytes, bytes_max, object_id)) != NULL &&
        (bytes = picoquic_frames_varint_encode(bytes, bytes_max, offset)) != NULL &&
        (bytes = picoquic_frames_varint_encode(bytes, bytes_max, object_length)) != NULL &&
        (bytes = picoquic_frames_uint8_encode(bytes, bytes_max, flags)) != NULL){
        if (object_id == 0 && offset == 0) {
            bytes = picoquic_frames_varint_encode(bytes, bytes_max, nb_objects_previous_group);
        }
        if (bytes != NULL) {
            if (data != NULL) {
                bytes = picoquic_frames_length_data_encode(bytes, bytes_max, length, data);
            }
            else {
                bytes = picoquic_frames_varint_encode(bytes, bytes_max, length);
            }
        }
    }
    return bytes;
}

const uint8_t* quicrq_fragment_msg_decode(const uint8_t* bytes, const uint8_t* bytes_max, uint64_t* message_type,
    uint64_t * group_id, uint64_t * object_id, uint64_t* nb_objects_previous_group,
    uint64_t* offset, uint64_t* object_length, uint8_t * flags, size_t * length, const uint8_t ** data)
{
    *group_id = 0;
    *object_id = 0;
    *nb_objects_previous_group = 0;
    *offset = 0;
    *object_length = 0;
    *length = 0;
    *data = NULL;
    if ((bytes = picoquic_frames_varint_decode(bytes, bytes_max, message_type)) != NULL &&
        (bytes = picoquic_frames_varint_decode(bytes, bytes_max, group_id)) != NULL &&
        (bytes = picoquic_frames_varint_decode(bytes, bytes_max, object_id)) != NULL &&
        (bytes = picoquic_frames_varint_decode(bytes, bytes_max, offset)) != NULL &&
        (bytes = picoquic_frames_varint_decode(bytes, bytes_max, object_length)) != NULL &&
        (bytes = picoquic_frames_uint8_decode(bytes, bytes_max, flags)) != NULL) {
        if (*object_id == 0 && *offset == 0) {
            bytes = picoquic_frames_varint_decode(bytes, bytes_max, nb_objects_previous_group);
        }
        if (bytes != NULL &&
            (bytes = picoquic_frames_varlen_decode(bytes, bytes_max, length)) != NULL) {
            *data = bytes;
            bytes = picoquic_frames_fixed_skip(bytes, bytes_max, *length);
        }
    }
    return bytes;
}

/* Encoding or decoding the start point message
 *
 * quicrq_start_point_message {
 *     message_type(i),
 *     start_group_id(i),
 *     start_object_id(i)
 * }
 */

size_t quicrq_start_point_msg_reserve(uint64_t start_group, uint64_t start_object)
{
    size_t len = 1 +
        picoquic_frames_varint_encode_length(start_group) +
        picoquic_frames_varint_encode_length(start_object);
    return len;
}


uint8_t* quicrq_start_point_msg_encode(uint8_t* bytes, uint8_t* bytes_max, uint64_t message_type, uint64_t start_group, uint64_t start_object)
{
    if ((bytes = picoquic_frames_varint_encode(bytes, bytes_max, message_type)) != NULL &&
        (bytes = picoquic_frames_varint_encode(bytes, bytes_max, start_group)) != NULL) {
        bytes = picoquic_frames_varint_encode(bytes, bytes_max, start_object);
    }
    return bytes;
}

const uint8_t* quicrq_start_point_msg_decode(const uint8_t* bytes, const uint8_t* bytes_max, uint64_t* message_type, uint64_t* start_group, uint64_t* start_object)
{
    if ((bytes = picoquic_frames_varint_decode(bytes, bytes_max, message_type)) != NULL &&
        (bytes = picoquic_frames_varint_decode(bytes, bytes_max, start_group)) != NULL){
        bytes = picoquic_frames_varint_decode(bytes, bytes_max, start_object);
    }
    return bytes;
}

/* Cache Policy Message
 *     message_type(i),
 *     cache_policy(8)
 */
size_t quicrq_cache_policy_msg_reserve()
{
    size_t len = 2;
    return len;
}


uint8_t* quicrq_cache_policy_msg_encode(uint8_t* bytes, uint8_t* bytes_max, uint64_t message_type, uint8_t cache_policy)
{
    if ((bytes = picoquic_frames_varint_encode(bytes, bytes_max, message_type)) != NULL) {
        bytes = picoquic_frames_uint8_encode(bytes, bytes_max, cache_policy);
    }
    return bytes;
}

const uint8_t* quicrq_cache_policy_msg_decode(const uint8_t* bytes, const uint8_t* bytes_max, uint64_t* message_type, uint8_t* cache_policy)
{
    if ((bytes = picoquic_frames_varint_decode(bytes, bytes_max, message_type)) != NULL){
        bytes = picoquic_frames_uint8_decode(bytes, bytes_max, cache_policy);
    }
    return bytes;
}

/* Media POST message.  
 *     message_type(i),
 *     url_length(i),
 *     url(...)
 *     transport_mode(i)
 *     cache_policy(8)
 *     start_group_id(i)
 *     start_object_id(i)
 *     
 * The post message is sent by a client when ready to push a media fragment.
 */

size_t quicrq_post_msg_reserve(size_t url_length)
{
    return  1 + 2 + url_length + 1 + 1 + 8 + 8;
}

uint8_t* quicrq_post_msg_encode(uint8_t* bytes, uint8_t* bytes_max, uint64_t message_type, size_t url_length,
    const uint8_t* url, quicrq_transport_mode_enum transport_mode, uint8_t cache_policy,
    uint64_t start_group_id, uint64_t start_object_id)
{
    if ((bytes = picoquic_frames_varint_encode(bytes, bytes_max, message_type)) != NULL &&
        (bytes = picoquic_frames_length_data_encode(bytes, bytes_max, url_length, url)) != NULL &&
        (bytes = picoquic_frames_varint_encode(bytes, bytes_max, (uint64_t)transport_mode)) != NULL &&
        (bytes = picoquic_frames_uint8_encode(bytes, bytes_max, cache_policy)) != NULL &&
        (bytes = picoquic_frames_varint_encode(bytes, bytes_max, start_group_id)) != NULL){
        bytes = picoquic_frames_varint_encode(bytes, bytes_max, start_object_id);
    }
    return bytes;
}

const uint8_t* quicrq_post_msg_decode(const uint8_t* bytes, const uint8_t* bytes_max, uint64_t* message_type, 
    size_t* url_length, const uint8_t** url, quicrq_transport_mode_enum* transport_mode, uint8_t * cache_policy,
    uint64_t* start_group_id, uint64_t* start_object_id)
{
    uint64_t t_mode = 0;
    *transport_mode = 0;
    *url = NULL;
    *url_length = 0;
    *start_group_id = 0;
    *start_object_id = 0;
    if ((bytes = picoquic_frames_varint_decode(bytes, bytes_max, message_type)) != NULL &&
        (bytes = picoquic_frames_varlen_decode(bytes, bytes_max, url_length)) != NULL) {
        *url = bytes;
        if ((bytes = picoquic_frames_fixed_skip(bytes, bytes_max, *url_length)) != NULL &&
            (bytes = picoquic_frames_varint_decode(bytes, bytes_max, &t_mode)) != NULL &&
            (bytes = picoquic_frames_uint8_decode(bytes, bytes_max, cache_policy)) != NULL &&
            (bytes = picoquic_frames_varint_decode(bytes, bytes_max, start_group_id)) != NULL  &&
            (bytes = picoquic_frames_varint_decode(bytes, bytes_max, start_object_id)) != NULL) {
            if (t_mode < quicrq_transport_mode_max) {
                *transport_mode = (quicrq_transport_mode_enum)t_mode;
            }
            else {
                bytes = NULL;
            }
        }
    }
    return bytes;
}

 /* Media ACCEPT message.
  *     message_type(i),
  *     transport_mode(i),
  *     [media_id(i)]
  *     
  * This is the response to the POST message. The server tells the client whether it
  * should send as datagrams or as stream, and if using streams send a datagram
  * stream ID.
  */

size_t quicrq_accept_msg_reserve(quicrq_transport_mode_enum transport_mode, uint64_t media_id)
{
    size_t len = 1 +
        picoquic_frames_varint_encode_length((uint64_t)transport_mode);
    if (transport_mode != quicrq_transport_mode_single_stream) {
        len += picoquic_frames_varint_encode_length(media_id);
    }
    return len;
}

uint8_t* quicrq_accept_msg_encode(uint8_t* bytes, uint8_t* bytes_max, uint64_t message_type, quicrq_transport_mode_enum transport_mode, uint64_t media_id)
{
    if ((bytes = picoquic_frames_varint_encode(bytes, bytes_max, message_type)) != NULL &&
        (bytes = picoquic_frames_varint_encode(bytes, bytes_max, (uint64_t)transport_mode)) != NULL) {
        if (transport_mode != quicrq_transport_mode_single_stream) {
            bytes = picoquic_frames_varint_encode(bytes, bytes_max, media_id);
        }
    }
    return bytes;
}

const uint8_t* quicrq_accept_msg_decode(const uint8_t* bytes, const uint8_t* bytes_max, uint64_t* message_type,
    quicrq_transport_mode_enum * transport_mode, uint64_t * media_id)
{
    uint64_t use_dg = 0;
    *transport_mode = 0;
    *media_id = 0;
    if ((bytes = picoquic_frames_varint_decode(bytes, bytes_max, message_type)) != NULL &&
        (bytes = picoquic_frames_varint_decode(bytes, bytes_max, &use_dg)) != NULL) {
        if (use_dg >= quicrq_transport_mode_max) {
            bytes = NULL;
        }
        else {
            *transport_mode = (quicrq_transport_mode_enum)use_dg;
            if (use_dg != quicrq_transport_mode_single_stream) {
                bytes = picoquic_frames_varint_decode(bytes, bytes_max, media_id);
            }
        }
    }

    return bytes;
}

/* Encoding or decoding the warp_header message
 *
 * quicrq_warp_header_message {
 *     message_type(i),
 *     media_id(i),
 *     group_id(i)
 * }
 */

size_t quicrq_warp_header_msg_reserve(uint64_t media_id, uint64_t group_id)
{
    size_t len = 1 +
                 picoquic_frames_varint_encode_length(media_id) +
                 picoquic_frames_varint_encode_length(group_id);
    return len;
}

uint8_t* quicrq_warp_header_msg_encode(uint8_t* bytes, uint8_t* bytes_max, uint64_t message_type, uint64_t media_id, uint64_t group_id)
{
    if ((bytes = picoquic_frames_varint_encode(bytes, bytes_max, message_type)) != NULL &&
        (bytes = picoquic_frames_varint_encode(bytes, bytes_max, media_id)) != NULL) {
        bytes = picoquic_frames_varint_encode(bytes, bytes_max, group_id);
    }
    return bytes;
}

const uint8_t* quicrq_warp_header_msg_decode(const uint8_t* bytes, const uint8_t* bytes_max, uint64_t* message_type, uint64_t* media_id, uint64_t* group_id)
{
    if ((bytes = picoquic_frames_varint_decode(bytes, bytes_max, message_type)) != NULL &&
        (bytes = picoquic_frames_varint_decode(bytes, bytes_max, media_id)) != NULL){
        bytes = picoquic_frames_varint_decode(bytes, bytes_max, group_id);
    }
    return bytes;
}


/* Encoding or decoding the object_header message
 *
 * quicrq_object_header_message {
 *     message_type(i),
 *     object_id(i),
 *     [nb_objects_previous_group(i),]
 *     flags[8],
 *     length(i),
 *     data(...)
 * }
 * 
 * The nb_objects_previous_group is only set if the object_id is zero.
 */

size_t quicrq_object_header_msg_reserve(uint64_t object_id, uint64_t nb_objects_previous_group, size_t data_length)
{
    size_t len = 1 + picoquic_frames_varint_encode_length(object_id) +
        ((object_id == 0) ? picoquic_frames_varint_encode_length(nb_objects_previous_group) : 0)
        + 1;
    len += picoquic_frames_varint_encode_length(data_length);
    len += data_length;
    return len;
}

uint8_t* quicrq_object_header_msg_encode(uint8_t* bytes, uint8_t* bytes_max, uint64_t message_type, uint64_t object_id,
    uint64_t nb_objects_previous_group, uint8_t flags, size_t length, const uint8_t *data)
{
    if ((bytes = picoquic_frames_varint_encode(bytes, bytes_max, message_type)) != NULL &&
        (bytes = picoquic_frames_varint_encode(bytes, bytes_max, object_id)) != NULL) {
        if (object_id == 0) {
            bytes = picoquic_frames_varint_encode(bytes, bytes_max, nb_objects_previous_group);
        }
        if (bytes != NULL) {
            if (bytes < bytes_max) {
                *bytes++ = flags;
                if (data != NULL) {
                    bytes = picoquic_frames_length_data_encode(bytes, bytes_max, length, data);
                }
                else {
                    bytes = picoquic_frames_varint_encode(bytes, bytes_max, length);
                }
            }
            else {
                bytes = NULL;
            }
        }
    }
    return bytes;
}

const uint8_t* quicrq_object_header_msg_decode(const uint8_t* bytes, const uint8_t* bytes_max, uint64_t* message_type,
    uint64_t *object_id, uint64_t *nb_objects_previous_group, uint8_t* flags, size_t *length, const uint8_t **data)
{
    *object_id = 0;
    *nb_objects_previous_group = 0;
    *flags = 0;
    *length = 0;
    *data = NULL;
    if ((bytes = picoquic_frames_varint_decode(bytes, bytes_max, message_type)) != NULL &&
        (bytes = picoquic_frames_varint_decode(bytes, bytes_max, object_id)) != NULL){
        if (*object_id == 0) {
            bytes = picoquic_frames_varint_decode(bytes, bytes_max, nb_objects_previous_group);
        }
        if (bytes != NULL) {
            if (bytes < bytes_max) {
                *flags = *bytes++;
                if ((bytes = picoquic_frames_varlen_decode(bytes, bytes_max, length)) != NULL) {
                    *data = bytes;
                    bytes = picoquic_frames_fixed_skip(bytes, bytes_max, *length);
                }
            }
            else {
                bytes = NULL;
            }
        }
    }
    return bytes;
}


/* Generic decoding of QUICRQ control message */
const uint8_t* quicrq_msg_decode(const uint8_t* bytes, const uint8_t* bytes_max, quicrq_message_t* msg)
{
    const uint8_t* bytes0 = bytes;
    memset(msg, 0, sizeof(quicrq_message_t));
    if ((bytes = picoquic_frames_varint_decode(bytes, bytes_max, &msg->message_type)) != NULL) {
        /* TODO: do not decode the message type twice */
        bytes = bytes0;
        switch (msg->message_type) {
        case QUICRQ_ACTION_REQUEST:
            bytes = quicrq_rq_msg_decode(bytes, bytes_max, &msg->message_type, &msg->url_length, &msg->url,
                &msg->media_id, &msg->transport_mode, &msg->subscribe_intent, &msg->group_id, &msg->object_id);
            break;
        case QUICRQ_ACTION_FIN_DATAGRAM:
            bytes = quicrq_fin_msg_decode(bytes, bytes_max, &msg->message_type, &msg->group_id, &msg->object_id);
            break;
        case QUICRQ_ACTION_FRAGMENT:
            bytes = quicrq_fragment_msg_decode(bytes, bytes_max, &msg->message_type,
                &msg->group_id, &msg->object_id, &msg->nb_objects_previous_group,
                &msg->fragment_offset, &msg->object_length, &msg->flags, &msg->fragment_length, &msg->data);
            break;
        case QUICRQ_ACTION_POST:
            bytes = quicrq_post_msg_decode(bytes, bytes_max, &msg->message_type, &msg->url_length, &msg->url,
                &msg->transport_mode, &msg->cache_policy, &msg->group_id, &msg->object_id);
            break;
        case QUICRQ_ACTION_ACCEPT:
            bytes = quicrq_accept_msg_decode(bytes, bytes_max, &msg->message_type, &msg->transport_mode, &msg->media_id);
            break;
        case QUICRQ_ACTION_START_POINT:
            bytes = quicrq_start_point_msg_decode(bytes, bytes_max, &msg->message_type, &msg->group_id, &msg->object_id);
            break;
        case QUICRQ_ACTION_SUBSCRIBE:
            bytes = quicrq_subscribe_msg_decode(bytes, bytes_max, &msg->message_type, &msg->url_length, &msg->url);
            break;
        case QUICRQ_ACTION_NOTIFY:
            bytes = quicrq_notify_msg_decode(bytes, bytes_max, &msg->message_type, &msg->url_length, &msg->url);
            break;
        case QUICRQ_ACTION_CACHE_POLICY:
            bytes = quicrq_cache_policy_msg_decode(bytes, bytes_max, &msg->message_type, &msg->cache_policy);
            break;
        case QUICRQ_ACTION_WARP_HEADER:
            bytes = quicrq_warp_header_msg_decode(bytes, bytes_max, &msg->message_type, &msg->media_id, &msg->group_id);
            break;
        case QUICRQ_ACTION_OBJECT_HEADER:
            bytes = quicrq_object_header_msg_decode(bytes, bytes_max, &msg->message_type, &msg->object_id,
                &msg->nb_objects_previous_group, &msg->flags, &msg->fragment_length, &msg->data);
            break;
        default:
            /* Unexpected message type */
            bytes = NULL;
            break;
        }
    }
    return bytes;
}

/* Generic encoding of QUICRQ control message */
uint8_t* quicrq_msg_encode(uint8_t* bytes, uint8_t* bytes_max, quicrq_message_t* msg)
{
    switch (msg->message_type) {
    case QUICRQ_ACTION_REQUEST:
        bytes = quicrq_rq_msg_encode(bytes, bytes_max, msg->message_type, msg->url_length, msg->url,
            msg->media_id, msg->transport_mode, msg->subscribe_intent, msg->group_id, msg->object_id);
        break;
    case QUICRQ_ACTION_FIN_DATAGRAM:
        bytes = quicrq_fin_msg_encode(bytes, bytes_max, msg->message_type, msg->group_id, msg->object_id);
        break;
    case QUICRQ_ACTION_FRAGMENT:
        bytes = quicrq_fragment_msg_encode(bytes, bytes_max,
            msg->message_type, msg->group_id, msg->object_id, msg->nb_objects_previous_group,
            msg->fragment_offset, msg->object_length, msg->flags, msg->fragment_length, msg->data);
        break;
    case QUICRQ_ACTION_POST:
        bytes = quicrq_post_msg_encode(bytes, bytes_max, msg->message_type, msg->url_length, msg->url,
            msg->transport_mode, msg->cache_policy, msg->group_id, msg->object_id);
        break;
    case QUICRQ_ACTION_ACCEPT:
        bytes = quicrq_accept_msg_encode(bytes, bytes_max, msg->message_type, msg->transport_mode, msg->media_id);
        break;
    case QUICRQ_ACTION_START_POINT:
        bytes = quicrq_start_point_msg_encode(bytes, bytes_max, msg->message_type, msg->group_id, msg->object_id);
        break;
    case QUICRQ_ACTION_SUBSCRIBE:
        bytes = quicrq_subscribe_msg_encode(bytes, bytes_max, msg->message_type, msg->url_length, msg->url);
        break;
    case QUICRQ_ACTION_NOTIFY:
        bytes = quicrq_notify_msg_encode(bytes, bytes_max, msg->message_type, msg->url_length, msg->url);
        break;
    case QUICRQ_ACTION_CACHE_POLICY:
        bytes = quicrq_cache_policy_msg_encode(bytes, bytes_max, msg->message_type, msg->cache_policy);
        break;
    case QUICRQ_ACTION_WARP_HEADER:
        bytes = quicrq_warp_header_msg_encode(bytes, bytes_max, msg->message_type, msg->media_id, msg->group_id);
        break;
    case QUICRQ_ACTION_OBJECT_HEADER:
        bytes = quicrq_object_header_msg_encode(bytes, bytes_max, msg->message_type, msg->object_id,
            msg->nb_objects_previous_group, msg->flags, msg->fragment_length, msg->data);
        break;
    default:
        /* Unexpected message type */
        bytes = NULL;
        break;
    }
    return bytes;
}


/* encoding of the datagram header
 * quicrq_datagram_header { 
 *     media_id (i)
 *     group_id (i)
 *     object_id (i)
 *     offset (i)
 *     object_length (i)
 *     queue_delay (i)
 *     flags (8)
 *     [nb_objects_previous_group (i)]
 * }
 */
uint8_t* quicrq_datagram_header_encode(uint8_t* bytes, uint8_t* bytes_max, uint64_t media_id, uint64_t group_id,
    uint64_t object_id, uint64_t object_offset, uint64_t queue_delay, uint8_t flags,
    uint64_t nb_objects_previous_group, uint64_t object_length)
{
    if ((bytes = picoquic_frames_varint_encode(bytes, bytes_max, media_id)) != NULL &&
        (bytes = picoquic_frames_varint_encode(bytes, bytes_max, group_id)) != NULL &&
        (bytes = picoquic_frames_varint_encode(bytes, bytes_max, object_id)) != NULL &&
        (bytes = picoquic_frames_varint_encode(bytes, bytes_max, object_offset)) != NULL &&
        (bytes = picoquic_frames_varint_encode(bytes, bytes_max, object_length)) != NULL &&
        (bytes = picoquic_frames_varint_encode(bytes, bytes_max, queue_delay)) != NULL){
        if (bytes < bytes_max) {
            *bytes++ = flags;
            if (object_id == 0 && object_offset == 0) {
                bytes = picoquic_frames_varint_encode(bytes, bytes_max, nb_objects_previous_group);
            }
        }
        else {
            bytes = NULL;
        }
    }
    return bytes;
}

const uint8_t* quicrq_datagram_header_decode(const uint8_t* bytes, const uint8_t* bytes_max, uint64_t* media_id, uint64_t* group_id,
    uint64_t* object_id, uint64_t* object_offset, uint64_t* queue_delay, uint8_t* flags, uint64_t* nb_objects_previous_group, uint64_t* object_length)
{
    if ((bytes = picoquic_frames_varint_decode(bytes, bytes_max, media_id)) != NULL &&
        (bytes = picoquic_frames_varint_decode(bytes, bytes_max, group_id)) != NULL &&
        (bytes = picoquic_frames_varint_decode(bytes, bytes_max, object_id)) != NULL &&
        (bytes = picoquic_frames_varint_decode(bytes, bytes_max, object_offset)) != NULL &&
        (bytes = picoquic_frames_varint_decode(bytes, bytes_max, object_length)) != NULL &&
        (bytes = picoquic_frames_varint_decode(bytes, bytes_max, queue_delay)) != NULL &&
        (bytes = picoquic_frames_uint8_decode(bytes, bytes_max, flags)) != NULL) {
        if (*object_id == 0 && *object_offset == 0) {
            bytes = picoquic_frames_varint_decode(bytes, bytes_max, nb_objects_previous_group);
        }
        else {
            *nb_objects_previous_group = 0;
        }
    }
    return bytes;
}

/* Publish local source API.
 */

quicrq_media_source_ctx_t* quicrq_publish_datagram_source(quicrq_ctx_t* qr_ctx, const uint8_t* url, size_t url_length,
    void* cache_ctx, int is_local_object_source, int is_cache_real_time)
{
    quicrq_media_source_ctx_t* srce_ctx = NULL;
    size_t source_ctx_size = sizeof(quicrq_media_source_ctx_t) + url_length;

    if (source_ctx_size >= sizeof(quicrq_media_source_ctx_t)) {
        srce_ctx = (quicrq_media_source_ctx_t*)malloc(source_ctx_size);

        if (srce_ctx != NULL) {
            memset(srce_ctx, 0, sizeof(quicrq_media_source_ctx_t));
            srce_ctx->media_url = ((uint8_t*)srce_ctx) + sizeof(quicrq_media_source_ctx_t);
            srce_ctx->media_url_length = url_length;
            memcpy(srce_ctx->media_url, url, url_length);
            srce_ctx->is_cache_real_time = is_cache_real_time;
            if (qr_ctx->last_source == NULL) {
                qr_ctx->first_source = srce_ctx;
                qr_ctx->last_source = srce_ctx;
            }
            else {
                qr_ctx->last_source->next_source = srce_ctx;
                srce_ctx->previous_source = qr_ctx->last_source;
                qr_ctx->last_source = srce_ctx;
            }
            srce_ctx->cache_ctx = cache_ctx;
            srce_ctx->is_local_object_source = is_local_object_source;

            // Called in the case there exists streams on the cnx
            // For publish object source, it is a no-op
            if (quicrq_notify_url_to_all(qr_ctx, url, url_length) < 0) {
                DBG_PRINTF("%s", "Fail to notify new source");
                quicrq_delete_source(srce_ctx, qr_ctx);
                srce_ctx = NULL;
            }
        }
    }

    return srce_ctx;
}

void quicrq_set_default_source(quicrq_ctx_t* qr_ctx, quicrq_default_source_fn default_source_fn, void* default_source_ctx)
{
    qr_ctx->default_source_fn = default_source_fn;
    qr_ctx->default_source_ctx = default_source_ctx;
}

void quicrq_delete_source(quicrq_media_source_ctx_t* srce_ctx, quicrq_ctx_t* qr_ctx)
{
    quicrq_stream_ctx_t* stream_ctx = srce_ctx->first_stream;

    while (stream_ctx != NULL) {
        quicrq_stream_ctx_t* next_stream_ctx = stream_ctx->next_stream_for_source;
        
        stream_ctx->next_stream_for_source = NULL;
        stream_ctx->previous_stream_for_source = NULL;
        

        stream_ctx = next_stream_ctx;
    }

    if (srce_ctx == qr_ctx->first_source) {
        qr_ctx->first_source = srce_ctx->next_source;
    }
    else {
        srce_ctx->previous_source->next_source = srce_ctx->next_source;
    }
    if (srce_ctx == qr_ctx->last_source) {
        qr_ctx->last_source = srce_ctx->previous_source;
    }
    else {
        srce_ctx->next_source->previous_source = srce_ctx->previous_source;
    }
    /* We support only one kind of source, so we just call the delete function */
    quicrq_fragment_publisher_delete(srce_ctx->cache_ctx);

    free(srce_ctx);
}

/* Set the default source, when appropriate */
quicrq_media_source_ctx_t* quicrq_create_default_source(quicrq_ctx_t* qr_ctx, const uint8_t* url, size_t url_length)
{
    quicrq_media_source_ctx_t* srce_ctx = NULL;
    int ret = 0;

    /* Call the default publisher function */
    if ((ret = qr_ctx->default_source_fn(qr_ctx->default_source_ctx, qr_ctx, url, url_length)) != 0) {
        /* Failure. The source returned an error */
    }
    else {
        /* Assume that the quicrq_publish_source function added the new source at the end of the list */
        srce_ctx = qr_ctx->last_source;
    }
    return srce_ctx;
}

/* Find whether the local context for a media source */
quicrq_media_source_ctx_t* quicrq_find_local_media_source(quicrq_ctx_t* qr_ctx, const uint8_t* url, const size_t url_length)
{
    quicrq_media_source_ctx_t* srce_ctx = qr_ctx->first_source;

    /* Find whether there is a matching media published locally */
    while (srce_ctx != NULL) {
        if (url_length == srce_ctx->media_url_length &&
            memcmp(url, srce_ctx->media_url, url_length) == 0) {
            break;
        }
        srce_ctx = srce_ctx->next_source;
    }
    return srce_ctx;
}

/* Parse incoming request, connect incoming stream to media source
 */
int quicrq_subscribe_local_media(quicrq_stream_ctx_t* stream_ctx, const uint8_t* url, const size_t url_length)
{
    int ret = 0;
    quicrq_ctx_t* qr_ctx = stream_ctx->cnx_ctx->qr_ctx;
    quicrq_media_source_ctx_t* srce_ctx = quicrq_find_local_media_source(qr_ctx, url, url_length);
    char buffer[256];

    if (srce_ctx == NULL && qr_ctx->default_source_fn != NULL) {
        srce_ctx = quicrq_create_default_source(qr_ctx, url, url_length);
    }
    if (srce_ctx == NULL) {
        ret = -1;
        quicrq_log_message(stream_ctx->cnx_ctx, "No source available for URL: %s",
            quicrq_uint8_t_to_text(url, url_length, buffer, 256));
    }
    else {
        /* Add stream to list of published streams */
        stream_ctx->media_source = srce_ctx;
        if (srce_ctx->last_stream == NULL) {
            srce_ctx->first_stream = stream_ctx;
            srce_ctx->last_stream = stream_ctx;
        }
        else {
            srce_ctx->last_stream->next_stream_for_source = stream_ctx;
            stream_ctx->previous_stream_for_source = srce_ctx->last_stream;
            srce_ctx->last_stream = stream_ctx;
        }
        /* set the cache policy */
        stream_ctx->is_cache_real_time = srce_ctx->is_cache_real_time;
        /* Create a subscribe media context */
        stream_ctx->media_ctx = quicrq_fragment_publisher_subscribe(srce_ctx->cache_ctx, stream_ctx);
        if (stream_ctx->media_ctx == NULL) {
            ret = -1;
            quicrq_log_message(stream_ctx->cnx_ctx, "No media available for URL: %s",
                quicrq_uint8_t_to_text(url, url_length, buffer, 256));
        }
        else {
            quicrq_log_message(stream_ctx->cnx_ctx, "Set a subscription to URL: %s",
                quicrq_uint8_t_to_text(url, url_length, buffer, 256));
        }
    }
    return ret;
}


/* When closing a stream, remove the stream from the source's wakeup list 
 */
void quicrq_unsubscribe_local_media(quicrq_stream_ctx_t* stream_ctx)
{
    quicrq_media_source_ctx_t* srce_ctx = stream_ctx->media_source;
    if (srce_ctx != NULL) {
        quicrq_stream_ctx_t* previous = stream_ctx->previous_stream_for_source;
        quicrq_stream_ctx_t* next = stream_ctx->next_stream_for_source;

        if (next != NULL) {
            next->previous_stream_for_source = previous;
        }
        else {
            srce_ctx->last_stream = previous;
        }

        if (previous != NULL) {
            previous->next_stream_for_source = next;
        }
        else {
            srce_ctx->first_stream = next;
        }
        stream_ctx->media_source = NULL;
        stream_ctx->previous_stream_for_source = NULL;
        stream_ctx->next_stream_for_source = NULL;
    }
}

static uint8_t quicrq_flags_to_picoquic_stream_priority(uint8_t flags, int use_fifo) {
    return ((flags & 0x7f) << 1) + use_fifo;
}

static void quicrq_set_control_stream_priority(quicrq_stream_ctx_t* stream_ctx)
{
    if (stream_ctx->media_ctx != NULL &&
        stream_ctx->media_ctx->cache_ctx != NULL &&
        stream_ctx->media_ctx->cache_ctx->lowest_flags > 0 && (
            stream_ctx->lowest_flags == 0 ||
            stream_ctx->media_ctx->cache_ctx->lowest_flags < stream_ctx->lowest_flags)) {
        uint8_t stream_priority;
        stream_ctx->lowest_flags = stream_ctx->media_ctx->cache_ctx->lowest_flags;
        stream_priority = quicrq_flags_to_picoquic_stream_priority(stream_ctx->lowest_flags, 0);
        (void)picoquic_set_stream_priority(stream_ctx->cnx_ctx->cnx, stream_ctx->stream_id, stream_priority);
    }
}

/* Waking up rush streams.
 * When implementing Rush, we handle one stream per object. 
 * The stream is created by observing the "in sequence" list of fragments, already
 * managed for the datagram case. The stream will be created upon reception of
 * the first fragment, i.e., offset 0. 
 * 
 * The stream priority could be set as a function of the object flag, which would
 * cause wakeup based on priority.
 * 
 * TODO: the first implementation does wake-up on whole objects. We should probably
 * wakeup on the arrival of fragments. This is also required for Warp.
 */

static void quicrq_set_rush_stream_priority(quicrq_uni_stream_ctx_t* uni_stream_ctx)
{
    if (uni_stream_ctx->stream_priority == 0) {
        uint8_t flags = quicrq_fragment_get_flags(uni_stream_ctx->control_stream_ctx->media_ctx->cache_ctx,
            uni_stream_ctx->current_group_id, uni_stream_ctx->current_object_id);

        if (flags != 0) {
            uni_stream_ctx->stream_priority = quicrq_flags_to_picoquic_stream_priority(flags, 1);
            (void)picoquic_set_stream_priority(uni_stream_ctx->control_stream_ctx->cnx_ctx->cnx,
                uni_stream_ctx->stream_id, uni_stream_ctx->stream_priority);
        }
    }
}

void quicrq_wakeup_media_rush_stream(quicrq_stream_ctx_t* stream_ctx)
{
    uint64_t highest_group_id = stream_ctx->media_ctx->cache_ctx->highest_group_id;
    uint64_t highest_object_id = stream_ctx->media_ctx->cache_ctx->highest_object_id;
    uint64_t old_highest_group_id = stream_ctx->next_warp_group_id;
    int stop_creating = 0;

    /* loop through all the unistreams, since more than one can be active */
    quicrq_uni_stream_ctx_t* uni_stream_ctx = stream_ctx->first_uni_stream;
    while (uni_stream_ctx != NULL) {
        if (uni_stream_ctx->send_state != quicrq_sending_warp_should_close) {
            /* TODO: the used contexts should be removed, so the test above will not be needed. */
            quicrq_set_rush_stream_priority(uni_stream_ctx);
            picoquic_mark_active_stream(uni_stream_ctx->control_stream_ctx->cnx_ctx->cnx, uni_stream_ctx->stream_id, 1, uni_stream_ctx);
        }
        /* todo, for rush: if header sent, only wake up if object is fully received. */
        uni_stream_ctx = uni_stream_ctx->next_uni_stream_for_control_stream;
    }

    /* create uni_streams for unseen group_id and object id from the cache.
     * TODO, for rush:
     * -- examine all groups
     * -- for all groups, check whether is last and if that last = next object + 1 or
     *    not, then last = next object for group. If next object for group is not known,
     *    wait.
     */
    for (uint64_t i = stream_ctx->next_warp_group_id;
        i <= highest_group_id && !stop_creating; i++) {
        uint64_t next_object_in_group = stream_ctx->next_rush_object_id;
        uint64_t last_object_in_group = highest_object_id +1;
        if (i > stream_ctx->next_warp_group_id) {
            stream_ctx->next_warp_group_id = i;
        }
        if (i > old_highest_group_id) {
            next_object_in_group = 0;
        }
        if (i < highest_group_id) {
            last_object_in_group = quicrq_fragment_get_object_count(stream_ctx->media_ctx->cache_ctx, i);
            if (last_object_in_group == 0) {
                /* stop creating uni streams until the next value is learned. */
                stop_creating = 1;
            }
        }
        for (uint64_t j = next_object_in_group; j < last_object_in_group; j++) {
            uint64_t uni_stream_id = picoquic_get_next_local_stream_id(stream_ctx->cnx_ctx->cnx, 1);
            quicrq_uni_stream_ctx_t* uni_stream_ctx = quicrq_find_or_create_uni_stream(
                uni_stream_id, stream_ctx->cnx_ctx, stream_ctx, 1);
            if (uni_stream_ctx != NULL) {
                uni_stream_ctx->current_group_id = i;
                uni_stream_ctx->current_object_id = j;
                uni_stream_ctx->last_object_id = j + 1;
                stream_ctx->next_rush_object_id = j + 1;
                /* set priority based on flags. */
                if (stream_ctx->lowest_flags == 0) {
                    quicrq_set_control_stream_priority(stream_ctx);
                }
                if (stream_ctx->lowest_flags != 0) {
                    quicrq_set_rush_stream_priority(uni_stream_ctx);
                }
                picoquic_mark_active_stream(uni_stream_ctx->control_stream_ctx->cnx_ctx->cnx,
                    uni_stream_ctx->stream_id, 1, uni_stream_ctx);
            }
            else {
                stop_creating = 1;
                break;
            }
        }
    }
}

void quicrq_wakeup_media_uni_stream(quicrq_stream_ctx_t* stream_ctx)
{
    uint64_t highest_group_id = stream_ctx->media_ctx->cache_ctx->highest_group_id;
    int uni_created = 0;

    /* loop through all the unistreams, since more than one can be active */
    quicrq_uni_stream_ctx_t* uni_stream_ctx = stream_ctx->first_uni_stream;
    while (uni_stream_ctx != NULL) {
        if (uni_stream_ctx->send_state != quicrq_sending_warp_should_close) {
            /* TODO: the used contexts should be removed, so the test above will not be needed. */
            picoquic_mark_active_stream(uni_stream_ctx->control_stream_ctx->cnx_ctx->cnx, uni_stream_ctx->stream_id, 1, uni_stream_ctx);
        }
        uni_stream_ctx = uni_stream_ctx->next_uni_stream_for_control_stream;
    }

    /* create uni_streams for unseen group_id from the cache */
    for (uint64_t i = stream_ctx->next_warp_group_id; i <= highest_group_id; i++) {
        uint64_t uni_stream_id = picoquic_get_next_local_stream_id(stream_ctx->cnx_ctx->cnx, 1);
        quicrq_uni_stream_ctx_t* ctx = quicrq_find_or_create_uni_stream(
            uni_stream_id, stream_ctx->cnx_ctx, stream_ctx, 1);
        if (ctx != NULL) {
            uni_created = 1;
            ctx->current_group_id = i;
            stream_ctx->next_warp_group_id = i + 1;
            picoquic_mark_active_stream(ctx->control_stream_ctx->cnx_ctx->cnx,
                ctx->stream_id, 1, ctx);
        }
        else {
            break;
        }
    }
    /* If a new stream id has been created, and the control stream priority is known:
     * - set the priority of the latest created stream to the stream priority
     * - reset the priority of the other streams 2 down (since bit 0 is the round robin mark)
     * Exception:
     * - if the congestion control is not set to "group", don't reset previous streams.
     * - if the stream priority is set to 0x80 (e.g., audio), don't reset the previous streams.
     */
    if (uni_created && stream_ctx->lowest_flags == 0) {
        quicrq_set_control_stream_priority(stream_ctx);
    }
    if (uni_created && stream_ctx->lowest_flags != 0) {
        uint8_t uni_stream_priority = quicrq_flags_to_picoquic_stream_priority(stream_ctx->lowest_flags, 0);
        uni_stream_ctx = stream_ctx->first_uni_stream;

        while (uni_stream_ctx != NULL) {
            if (uni_stream_ctx->send_state != quicrq_sending_warp_should_close) {
                if (uni_stream_ctx->current_group_id == highest_group_id) {
                    (void)picoquic_set_stream_priority(stream_ctx->cnx_ctx->cnx, uni_stream_ctx->stream_id, uni_stream_priority);
                    uni_stream_ctx->stream_priority = uni_stream_priority;
                }
                else
                {
                    uint8_t lower_uni_stream_priority = uni_stream_priority;
                    if (stream_ctx->media_ctx != NULL && stream_ctx->media_ctx->congestion_control_mode == quicrq_congestion_control_group_p &&
                        stream_ctx->lowest_flags != 0x80 && lower_uni_stream_priority < 0xfe) {
                        lower_uni_stream_priority += 2;
                    }
                    if (uni_stream_ctx->stream_priority != lower_uni_stream_priority) {
                        (void)picoquic_set_stream_priority(stream_ctx->cnx_ctx->cnx, uni_stream_ctx->stream_id, lower_uni_stream_priority);
                        uni_stream_ctx->stream_priority = lower_uni_stream_priority;
                    }
                }
            }
            uni_stream_ctx = uni_stream_ctx->next_uni_stream_for_control_stream;
        }
    }
}

/// stream_ctx - this is for control channel
void quicrq_wakeup_media_stream(quicrq_stream_ctx_t* stream_ctx)
{
    if (stream_ctx->cnx_ctx->cnx != NULL) {
        if (stream_ctx->transport_mode == quicrq_transport_mode_single_stream) {
            picoquic_mark_active_stream(stream_ctx->cnx_ctx->cnx, stream_ctx->stream_id, 1, stream_ctx);
        }
        else {
            if (!stream_ctx->is_final_object_id_sent && stream_ctx->media_ctx != NULL &&
                stream_ctx->media_ctx->cache_ctx != NULL &&
                (stream_ctx->media_ctx->cache_ctx->final_group_id != 0 ||
                    stream_ctx->media_ctx->cache_ctx->final_object_id != 0)) {
                stream_ctx->final_group_id = stream_ctx->media_ctx->cache_ctx->final_group_id;
                stream_ctx->final_object_id = stream_ctx->media_ctx->cache_ctx->final_object_id;
            }
            if (((stream_ctx->start_group_id != 0 || stream_ctx->start_object_id != 0) &&
                !stream_ctx->is_start_object_id_sent) ||
                (stream_ctx->is_cache_real_time && !stream_ctx->is_cache_policy_sent) ||
                (!stream_ctx->is_final_object_id_sent &&
                    ( stream_ctx->final_group_id != 0 ||
                        stream_ctx->final_object_id != 0))){
                picoquic_mark_active_stream(stream_ctx->cnx_ctx->cnx, stream_ctx->stream_id, 1, stream_ctx);
                quicrq_set_control_stream_priority(stream_ctx);
            }

            if (stream_ctx->transport_mode == quicrq_transport_mode_datagram) {
                stream_ctx->is_active_datagram = 1;
                picoquic_mark_datagram_ready(stream_ctx->cnx_ctx->cnx, 1);
            }
            else if (stream_ctx->transport_mode == quicrq_transport_mode_warp) {
                /* handle case of handling warp mode */
                /*
                 * Scenarios
                 *  TODO: Can it support out of order sending of groups
                 *  TODO: mix of transport types (datagram & stream) along the path and its implications
                 *        on the cache organization
                 */
                if (stream_ctx->is_sender && stream_ctx->media_id != UINT64_MAX) {
                    quicrq_wakeup_media_uni_stream(stream_ctx);
                }
            }
            else if (stream_ctx->transport_mode == quicrq_transport_mode_rush) {
                /* handle case of handling warp mode */
                /*
                * Scenarios
                *  TODO: Can it support out of order sending of groups
                *  TODO: mix of transport types (datagram & stream) along the path and its implications
                *        on the cache organization
                */
                if (stream_ctx->is_sender && stream_ctx->media_id != UINT64_MAX) {
                    quicrq_wakeup_media_rush_stream(stream_ctx);
                }
            }
            else {
                DBG_PRINTF("Wake up for unexpected transport mode: %d (%s)",
                    (int)stream_ctx->transport_mode, quicrq_transport_mode_to_string(stream_ctx->transport_mode));
            }
        }
    }
}


/* When data is available for a source, wake up the corresponding connection 
 * and possibly stream.
 * TODO: for datagram, we may want to manage a queue of media for which data is ready.
 */
void quicrq_source_wakeup(quicrq_media_source_ctx_t* srce_ctx)
{
    quicrq_stream_ctx_t* stream_ctx = srce_ctx->first_stream;
    while (stream_ctx != NULL) {
        quicrq_wakeup_media_stream(stream_ctx);
        stream_ctx = stream_ctx->next_stream_for_source;
    }
}

/* Request media in connection.
 * Send a media request to the server.
 */
int quicrq_cnx_subscribe_media_ex(quicrq_cnx_ctx_t* cnx_ctx, const uint8_t* url, size_t url_length,
    quicrq_transport_mode_enum transport_mode, const quicrq_subscribe_intent_t * intent,
    quicrq_media_consumer_fn media_consumer_fn, void* media_ctx, quicrq_stream_ctx_t** p_stream_ctx)
{
    /* Create a stream for the media */
    int ret = 0;
    uint64_t stream_id = picoquic_get_next_local_stream_id(cnx_ctx->cnx, 0);
    quicrq_stream_ctx_t* stream_ctx = quicrq_create_stream_context(cnx_ctx, stream_id);
    quicrq_message_buffer_t* message = &stream_ctx->message_sent;
    static const quicrq_subscribe_intent_t default_intent = { quicrq_subscribe_intent_start_point, 0, 0 };

    if (intent == NULL) {
        intent = &default_intent;
    }

    if (stream_ctx == NULL) {
        ret = -1;
    }
    else {
        if (quicrq_msg_buffer_alloc(message, quicrq_rq_msg_reserve(url_length, intent->intent_mode), 0) != 0) {
            ret = -1;
        }
        else {
            /* Format the media request */
            uint64_t media_id = stream_ctx->cnx_ctx->next_media_id;
            uint8_t* message_next = quicrq_rq_msg_encode(message->buffer, message->buffer + message->buffer_alloc,
                QUICRQ_ACTION_REQUEST, url_length, url, media_id, transport_mode,
                intent->intent_mode, intent->start_group_id, intent->start_object_id);
            if (message_next == NULL) {
                ret = -1;
            } else {
                char buffer[256];
                /* Queue the media request message to that stream */
                stream_ctx->transport_mode = transport_mode;
                stream_ctx->media_id = media_id;
                message->message_size = message_next - message->buffer;
                stream_ctx->consumer_fn = media_consumer_fn;
                stream_ctx->media_ctx = media_ctx;
                stream_ctx->send_state = quicrq_sending_initial;
                stream_ctx->receive_state = quicrq_receive_fragment;
                stream_ctx->cnx_ctx->next_media_id += 1;
                if (p_stream_ctx != NULL) {
                    *p_stream_ctx = stream_ctx;
                }
                picoquic_mark_active_stream(cnx_ctx->cnx, stream_id, 1, stream_ctx);
                quicrq_log_message(cnx_ctx, "Posting subscribe to URL: %s on stream %" PRIu64,
                    quicrq_uint8_t_to_text(url, url_length, buffer, 256), stream_ctx->stream_id);
            }
        }
    }
    return ret;
}

int quicrq_cnx_subscribe_media(quicrq_cnx_ctx_t* cnx_ctx, const uint8_t* url, size_t url_length,
    quicrq_transport_mode_enum transport_mode, quicrq_media_consumer_fn media_consumer_fn, void* media_ctx)
{
    return quicrq_cnx_subscribe_media_ex(cnx_ctx, url, url_length,
        transport_mode, NULL, media_consumer_fn, media_ctx, NULL);
}

/* Process an incoming subscribe command */
int quicrq_cnx_connect_media_source(quicrq_stream_ctx_t* stream_ctx, uint8_t * url, size_t url_length, unsigned int use_datagram)
{
    int ret = 0;
    /* Process initial request */
    /* Open the media -- TODO, variants with different actions. */
    ret = quicrq_subscribe_local_media(stream_ctx, url, url_length);
    if (ret == 0) {
        quicrq_wakeup_media_stream(stream_ctx);
    }
    stream_ctx->is_sender = 1;
    if (stream_ctx->transport_mode == quicrq_transport_mode_single_stream) {
        stream_ctx->send_state = quicrq_sending_single_stream;
        stream_ctx->receive_state = quicrq_receive_done;
        picoquic_mark_active_stream(stream_ctx->cnx_ctx->cnx, stream_ctx->stream_id, 1, stream_ctx);
    }
    else {
        /* There is no data to send or receive on the control stream at this point.
         * The sender will send a final object eventually.
         * The receiver will close the stream when not needed anymore. */
        stream_ctx->send_state = quicrq_sending_ready;
        stream_ctx->receive_state = quicrq_receive_done;
    }

    return ret;
}

/* Post a local media */
int quicrq_cnx_post_media(quicrq_cnx_ctx_t* cnx_ctx, const uint8_t* url, size_t url_length,
    quicrq_transport_mode_enum transport_mode)
{
    /* Create a stream for the media */
    int ret = 0;
    uint64_t stream_id = picoquic_get_next_local_stream_id(cnx_ctx->cnx, 0);
    quicrq_stream_ctx_t* stream_ctx = quicrq_create_stream_context(cnx_ctx, stream_id);
    quicrq_message_buffer_t* message = &stream_ctx->message_sent;

    if (stream_ctx == NULL) {
        ret = -1;
    }
    else {
        if (quicrq_msg_buffer_alloc(message, quicrq_post_msg_reserve(url_length), 0) != 0) {
            ret = -1;
        }
        else {
            ret = quicrq_subscribe_local_media(stream_ctx, url, url_length);
            if (ret == 0) {
                /* Format the post message */
                uint8_t* message_next = quicrq_post_msg_encode(message->buffer, message->buffer + message->buffer_alloc,
                    QUICRQ_ACTION_POST, url_length, url, transport_mode, stream_ctx->is_cache_real_time,
                    stream_ctx->start_group_id, stream_ctx->start_object_id);
                if (message_next == NULL) {
                    ret = -1;
                }
                else {
                    char url_text[256];

                    quicrq_log_message(stream_ctx->cnx_ctx, "Stream %" PRIu64 ", post media url %s, mode = %s",
                        stream_ctx->stream_id, quicrq_uint8_t_to_text(url, url_length, url_text, 256),
                        quicrq_transport_mode_to_string(transport_mode));
                    /* Queue the post message to that stream */
                    stream_ctx->is_sender = 1;
                    stream_ctx->is_cache_policy_sent = stream_ctx->is_cache_real_time;
                    stream_ctx->is_start_object_id_sent = (stream_ctx->start_group_id > 0 || stream_ctx->start_object_id > 0);
                    message->message_size = message_next - message->buffer;
                    stream_ctx->send_state = quicrq_sending_initial;
                    stream_ctx->receive_state = quicrq_receive_confirmation;
                    stream_ctx->media_id = UINT64_MAX;
                    stream_ctx->transport_mode = transport_mode;
                    stream_ctx->next_group_id = stream_ctx->start_group_id;
                    stream_ctx->next_object_id = stream_ctx->start_object_id;
                    picoquic_mark_active_stream(cnx_ctx->cnx, stream_id, 1, stream_ctx);
                }
            }
        }
    }
    return ret;
}

int quicrq_set_media_init_callback(quicrq_ctx_t* ctx, quicrq_media_consumer_init_fn media_init_fn)
{
    ctx->consumer_media_init_fn = media_init_fn;

    return 0;
}

int quicrq_set_media_stream_ctx(quicrq_stream_ctx_t* stream_ctx, quicrq_media_consumer_fn consumer_fn, void* media_ctx)
{
    stream_ctx->consumer_fn = consumer_fn;
    stream_ctx->media_ctx = media_ctx;

    return 0;
}

/* Accept a media post and connect it to the local consumer */
int quicrq_cnx_accept_media(quicrq_stream_ctx_t * stream_ctx, const uint8_t* url, size_t url_length,
    quicrq_transport_mode_enum transport_mode, uint8_t cache_policy, uint64_t start_group_id, uint64_t start_object_id)
{
    int ret = 0;
    quicrq_message_buffer_t* message = &stream_ctx->message_sent;
    uint64_t media_id = (transport_mode == quicrq_transport_mode_single_stream)?0:stream_ctx->cnx_ctx->next_media_id;

    /* Format the accept message */
    if (quicrq_msg_buffer_alloc(message, quicrq_accept_msg_reserve(transport_mode, media_id), 0) != 0) {
        ret = -1;
    }
    else {
        uint8_t* message_next = quicrq_accept_msg_encode(message->buffer, message->buffer + message->buffer_alloc,
            QUICRQ_ACTION_ACCEPT, transport_mode, media_id);
        if (message_next == NULL) {
            ret = -1;
        }
        else {
            /* Queue the accept message to that stream */
            char buffer[256];
            stream_ctx->transport_mode = transport_mode;
            message->message_size = message_next - message->buffer;
            stream_ctx->send_state = quicrq_sending_initial;
            stream_ctx->receive_state = quicrq_receive_fragment;
            if (transport_mode != quicrq_transport_mode_single_stream) {
                stream_ctx->media_id = media_id;
                stream_ctx->cnx_ctx->next_media_id += 1;
            }
            /* Connect to the local listener */
            ret = stream_ctx->cnx_ctx->qr_ctx->consumer_media_init_fn(stream_ctx, url, url_length);
            /* Activate the receiver */
            picoquic_mark_active_stream(stream_ctx->cnx_ctx->cnx, stream_ctx->stream_id, 1, stream_ctx);
            quicrq_log_message(stream_ctx->cnx_ctx, "Accepted post of URL: %s on stream %" PRIu64,
                quicrq_uint8_t_to_text(url, url_length, buffer, 256), stream_ctx->stream_id);
            /* Set the cache policy for the local media */
            if (ret == 0 && cache_policy != 0) {
                ret = stream_ctx->consumer_fn(quicrq_media_real_time_cache, stream_ctx->media_ctx, picoquic_get_quic_time(stream_ctx->cnx_ctx->qr_ctx->quic),
                    NULL, 0, 0, 0, 0, 0, 0, 0, 0);
            }
            /* Set the initial group and object id for the local media */
            if (start_group_id != 0 || start_object_id != 0) {
                quicrq_log_message(stream_ctx->cnx_ctx,
                    "Stream %" PRIu64 ", start point notified: %" PRIu64 "/%" PRIu64,
                    stream_ctx->stream_id, start_group_id, start_object_id);
                stream_ctx->start_group_id = start_group_id;
                stream_ctx->start_object_id = start_object_id;
                ret = stream_ctx->consumer_fn(quicrq_media_start_point, stream_ctx->media_ctx, picoquic_get_quic_time(stream_ctx->cnx_ctx->qr_ctx->quic),
                    NULL, start_group_id, start_object_id, 0, 0, 0, 0, 0, 0);
            }
        }
    }

    return ret;
}

/* Accept confirmation of a media post and prepare to receive */
int quicrq_cnx_post_accepted(quicrq_stream_ctx_t* stream_ctx, quicrq_transport_mode_enum transport_mode, uint64_t media_id)
{
    int ret = 0;
    /* Confirm the datagram or stream status */
    stream_ctx->receive_state = quicrq_receive_fragment;
    stream_ctx->is_sender = 1;
    stream_ctx->transport_mode = transport_mode;
    if (transport_mode == quicrq_transport_mode_datagram) {
        stream_ctx->media_id = media_id;
        stream_ctx->send_state = quicrq_sending_ready;
        stream_ctx->receive_state = quicrq_receive_done;
        /* Maybe we need to send policy messages, in which case the stream should be active! */
        int more_to_send = (!stream_ctx->is_start_object_id_sent && (stream_ctx->start_group_id > 0 || stream_ctx->start_object_id > 0));
        more_to_send |= (!stream_ctx->is_cache_policy_sent && stream_ctx->is_cache_real_time);
        quicrq_log_message(stream_ctx->cnx_ctx, "Stream %" PRIu64 ", post accepted, start= %" PRIu64 "/%" PRIu64 " %s",
            stream_ctx->stream_id, stream_ctx->start_group_id, stream_ctx->start_object_id,
            (stream_ctx->is_start_object_id_sent) ? "(already sent)":"");
        picoquic_mark_active_stream(stream_ctx->cnx_ctx->cnx, stream_ctx->stream_id, more_to_send, stream_ctx);
    }
    else if (transport_mode == quicrq_transport_mode_single_stream){
        stream_ctx->send_state = quicrq_sending_single_stream;
        stream_ctx->receive_state = quicrq_receive_done;
        picoquic_mark_active_stream(stream_ctx->cnx_ctx->cnx, stream_ctx->stream_id, 1, stream_ctx);
    } else if (transport_mode == quicrq_transport_mode_warp || transport_mode == quicrq_transport_mode_rush) {
        stream_ctx->media_id = media_id;
        stream_ctx->send_state = quicrq_sending_ready;
        stream_ctx->receive_state = quicrq_receive_done;
        picoquic_mark_active_stream(stream_ctx->cnx_ctx->cnx, stream_ctx->stream_id, 1, stream_ctx);

    } else {
        /* TODO: WARP, RUSH */
        ret = -1;
    }
    quicrq_wakeup_media_stream(stream_ctx);
    return ret;
}

/* Mark a stream as finished after receiving the repair indication */
int quicrq_cnx_handle_consumer_finished(quicrq_stream_ctx_t* stream_ctx, int is_final, int is_datagram, int ret)
{
    if (ret == quicrq_consumer_finished) {
        quicrq_log_message(stream_ctx->cnx_ctx, "Stream %"PRIu64", %s, finished after %s, ret=%d",
            stream_ctx->stream_id, quicrq_transport_mode_to_string(stream_ctx->transport_mode),
            (is_final) ? "final offset" : ((is_datagram) ? "datagram" : "repair"), ret);
        DBG_PRINTF("Stream %"PRIu64" finished after %s, ret=%d", stream_ctx->stream_id, (is_final)?"final offset":((is_datagram)?"datagram":"repair"), ret);
        stream_ctx->is_receive_complete = 1;
        stream_ctx->send_state = quicrq_sending_fin;
        if (stream_ctx->close_reason == quicrq_media_close_reason_unknown) {
            stream_ctx->close_reason = quicrq_media_close_finished;
        }
        picoquic_mark_active_stream(stream_ctx->cnx_ctx->cnx, stream_ctx->stream_id, 1, stream_ctx);
        ret = 0;
    }
    return ret;
}

/* Abandon a stream before receive is complete */
void quicrq_cnx_abandon_stream(quicrq_stream_ctx_t* stream_ctx)
{
    stream_ctx->send_state = quicrq_sending_fin;
    (void)picoquic_mark_active_stream(stream_ctx->cnx_ctx->cnx, stream_ctx->stream_id, 1, stream_ctx);
    if (stream_ctx->transport_mode == quicrq_transport_mode_datagram && !stream_ctx->is_sender) {
        if (stream_ctx->cnx_ctx->next_abandon_datagram_id <= stream_ctx->media_id) {
            stream_ctx->cnx_ctx->next_abandon_datagram_id = stream_ctx->media_id + 1;
        }
    }
}

void quicrq_cnx_abandon_stream_id(quicrq_cnx_ctx_t * cnx_ctx, uint64_t stream_id)
{
    quicrq_stream_ctx_t* stream_ctx = quicrq_find_or_create_stream(stream_id, cnx_ctx, 0);
    if (stream_ctx != NULL) {
        quicrq_cnx_abandon_stream(stream_ctx);
    }
}
