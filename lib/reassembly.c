/* Handling of a relay
 */

#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include "picoquic_utils.h"
#include "picosplay.h"
#include "quicrq.h"
#include "quicrq_internal.h"
#include "quicrq_reassembly.h"

 /* Media receiver definitions.
  * Manage a list of frames being reassembled. The list is organized as a splay,
  * indexed by the frame id and frame offset. When a new fragment is received
  * the code will check whether the frame is already present, and then whether the
  * fragment for that frame has already arrived.
  */

/* Define data types used by implementation of public reassembly API */

typedef struct st_quicrq_reassembly_packet_t {
    struct st_quicrq_reassembly_packet_t* next_packet;
    struct st_quicrq_reassembly_packet_t* previous_packet;
    uint64_t current_time;
    uint8_t* data;
    uint64_t offset;
    size_t data_length;
} quicrq_reassembly_packet_t;

typedef struct st_quicrq_reassembly_frame_t {
    picosplay_node_t frame_node;
    struct st_quicrq_reassembly_packet_t* first_packet;
    struct st_quicrq_reassembly_packet_t* last_packet;
    uint64_t frame_id;
    uint64_t final_offset;
    uint64_t data_received;
    uint64_t last_update_time;
    uint8_t* reassembled;
} quicrq_reassembly_frame_t;

/* manage the splay of frames waiting reassembly */

static void* quicrq_frame_node_value(picosplay_node_t* frame_node)
{
    return (frame_node == NULL) ? NULL : (void*)((char*)frame_node - offsetof(struct st_quicrq_reassembly_frame_t, frame_node));
}

static int64_t quicrq_frame_node_compare(void* l, void* r) {
    return (int64_t)((quicrq_reassembly_frame_t*)l)->frame_id - ((quicrq_reassembly_frame_t*)r)->frame_id;
}

static picosplay_node_t* quicrq_frame_node_create(void* v_media_frame)
{
    return &((quicrq_reassembly_frame_t*)v_media_frame)->frame_node;
}

static void quicrq_frame_node_delete(void* tree, picosplay_node_t* node)
{
#ifdef _WINDOWS
    UNREFERENCED_PARAMETER(tree);
#endif
    memset(node, 0, sizeof(picosplay_node_t));
}

void quicrq_reassembly_init(quicrq_reassembly_context_t* frame_list)
{
    picosplay_init_tree(&frame_list->frame_tree, quicrq_frame_node_compare,
        quicrq_frame_node_create, quicrq_frame_node_delete, quicrq_frame_node_value);
}

/* Free the reassembly context
 */
void quicrq_reassembly_release(quicrq_reassembly_context_t* reassembly_ctx)
{
    picosplay_empty_tree(&reassembly_ctx->frame_tree);
    memset(reassembly_ctx, 0, sizeof(quicrq_reassembly_context_t));
}

static quicrq_reassembly_frame_t* quicrq_frame_find(quicrq_reassembly_context_t* frame_list, uint64_t frame_id)
{
    quicrq_reassembly_frame_t* frame = NULL;
    quicrq_reassembly_frame_t key_frame = { 0 };
    key_frame.frame_id = frame_id;
    picosplay_node_t* node = picosplay_find(&frame_list->frame_tree, (void*)&key_frame);
    if (node != NULL) {
        frame = (quicrq_reassembly_frame_t*)quicrq_frame_node_value(node);
    }
    return frame;
}

/* Management of the list of frames undergoing reassembly, frame-id based logic */
static quicrq_reassembly_packet_t* quicrq_reassembly_frame_create_packet(
    quicrq_reassembly_frame_t* frame,
    quicrq_reassembly_packet_t* previous_packet,
    uint64_t current_time,
    const uint8_t* data,
    uint64_t offset,
    size_t data_length)
{
    quicrq_reassembly_packet_t* packet = NULL;
    size_t packet_size = sizeof(quicrq_reassembly_packet_t) + data_length;
    if (packet_size >= sizeof(quicrq_reassembly_packet_t)) {
        packet = (quicrq_reassembly_packet_t*)malloc(packet_size);
        if (packet != NULL) {
            memset(packet, 0, sizeof(quicrq_reassembly_packet_t));
            packet->current_time = current_time;
            packet->offset = offset;
            packet->data_length = data_length;
            packet->data = ((uint8_t*)packet) + sizeof(quicrq_reassembly_packet_t);
            memcpy(packet->data, data, data_length);

            packet->previous_packet = previous_packet;
            if (previous_packet == NULL) {
                packet->next_packet = frame->first_packet;
                frame->first_packet = packet;
            }
            else {
                packet->next_packet = previous_packet->next_packet;
                previous_packet->next_packet = packet;
            }
            if (packet->next_packet == NULL) {
                frame->last_packet = packet;
            }
            else {
                packet->next_packet->previous_packet = packet;
            }
            frame->data_received += data_length;
            frame->last_update_time = current_time;
        }
    }

    return packet;
}

static quicrq_reassembly_frame_t* quicrq_reassembly_frame_create(quicrq_reassembly_context_t* reassembly_ctx, uint64_t frame_id)
{
    quicrq_reassembly_frame_t* frame = (quicrq_reassembly_frame_t*)malloc(sizeof(quicrq_reassembly_frame_t));
    if (frame != NULL) {
        memset(frame, 0, sizeof(quicrq_reassembly_frame_t));
        frame->frame_id = frame_id;
        picosplay_insert(&reassembly_ctx->frame_tree, frame);
    }
    return frame;
}

static void quicrq_reassembly_frame_delete(quicrq_reassembly_context_t* reassembly_ctx, quicrq_reassembly_frame_t* frame)
{
    /* Free the frame's resource */
    quicrq_reassembly_packet_t* packet;

    if (frame->reassembled != NULL) {
        free(frame->reassembled);
    }

    while ((packet = frame->first_packet) != NULL) {
        frame->first_packet = packet->next_packet;
        free(packet);
    }

    /* Remove the frame from the list */
    picosplay_delete_hint(&reassembly_ctx->frame_tree, &frame->frame_node);

    /* and free the memory */
    free(frame);
}

static int quicrq_reassembly_frame_add_packet(
    quicrq_reassembly_frame_t* frame,
    uint64_t current_time,
    const uint8_t* data,
    uint64_t offset,
    size_t data_length)
{
    int ret = 0;
    quicrq_reassembly_packet_t* packet = frame->first_packet;
    quicrq_reassembly_packet_t* previous_packet = NULL;

    while (packet != NULL) {
        if (packet->offset >= offset) {
            /* filling a hole */
            if (offset + data_length <= packet->offset) {
                /* No overlap. Just insert the packet after the previous one */
                quicrq_reassembly_packet_t* new_packet = quicrq_reassembly_frame_create_packet(frame, previous_packet, current_time, data, offset, data_length);
                if (new_packet == NULL) {
                    ret = -1;
                }
                else {
                    data_length = 0;
                }
                break;
            }
            else if (offset < packet->offset) {
                /* partial overlap. Create a packet for the non overlapping part, then retain the bytes at the end. */
                size_t consumed = (size_t)(packet->offset - offset);
                quicrq_reassembly_packet_t* new_packet = quicrq_reassembly_frame_create_packet(frame, previous_packet, current_time, data, offset, consumed);
                if (new_packet == NULL) {
                    ret = -1;
                    break;
                }
                else {
                    /* Trim the data. First remove the part that was consumed */
                    data += consumed;
                    offset += consumed;
                    data_length -= consumed;
                }
            }
        }
        /* At this point, we know the incoming data is at or after the current packet */
        if (packet->offset + packet->data_length > offset) {
            /* at least partial overlap */
            if (packet->offset + packet->data_length >= offset + data_length) {
                /* all remaining data is redundant */
                data_length = 0;
                break;
            }
            else {
                size_t consumed = (size_t)(packet->offset + packet->data_length - offset);
                data += consumed;
                offset += consumed;
                data_length -= consumed;
            }
        }
        /* after next packet, no overlap, need to continue the loop */
        previous_packet = packet;
        packet = packet->next_packet;
    }
    /* All packets in store have been checked */
    if (ret == 0 && data_length > 0) {
        /* Some of the incoming data was not inserted */
        quicrq_reassembly_packet_t* new_packet = quicrq_reassembly_frame_create_packet(frame, previous_packet, current_time, data, offset, data_length);
        if (new_packet == NULL) {
            ret = -1;
        }
        else {
            data_length = 0;
        }
    }

    return ret;
}

static int quicrq_reassembly_frame_reassemble(quicrq_reassembly_frame_t* frame)
{
    int ret = 0;

    /* Check that that the received bytes are in order */
    if (frame->final_offset == 0 || frame->data_received != frame->final_offset) {
        ret = -1;
    }
    else if (frame->first_packet == NULL || frame->first_packet->offset != 0) {
        ret = -1;
    }
    else if (frame->last_packet == NULL ||
        frame->last_packet->offset + frame->last_packet->data_length != frame->final_offset) {
        ret = -1;
    }
    else if (frame->final_offset > SIZE_MAX) {
        ret = -1;
    }
    else {
        frame->reassembled = (uint8_t*)malloc((size_t)frame->final_offset);
        if (frame->reassembled == NULL) {
            ret = -1;
        }
        else {
            size_t running_offset = 0;
            quicrq_reassembly_packet_t* packet = frame->first_packet;
            while (packet != NULL && ret == 0) {
                /* TODO: the "running offset" checks are never supposed to fire, unless
                 * there is a bug in the fragment collection program. Should be removed
                 * once debugging is complete */
                if (packet->offset != running_offset) {
                    ret = -1;
                }
                else if (running_offset + packet->data_length > frame->final_offset) {
                    ret = -1;
                }
                else {
                    memcpy(frame->reassembled + running_offset, packet->data, packet->data_length);
                    running_offset += packet->data_length;
                    packet = packet->next_packet;
                }
            }
            /* Final check also is just for debugging, should never fire */
            if (ret == 0 && running_offset != frame->final_offset) {
                ret = -1;
            }
        }
    }
    return ret;
}

int quicrq_reassembly_input(
    quicrq_reassembly_context_t* reassembly_ctx,
    uint64_t current_time,
    const uint8_t* data,
    uint64_t frame_id,
    uint64_t offset,
    int is_last_fragment,
    size_t data_length,
    quicrq_reassembly_frame_ready_fn ready_fn,
    void* app_media_ctx)
{
    int ret = 0;
    if (frame_id < reassembly_ctx->next_frame_id) {
        /* No need for this frame. */
    }
    else {
        quicrq_reassembly_frame_t* frame = quicrq_frame_find(reassembly_ctx, frame_id);

        if (frame == NULL) {
            /* Create a media frame for reassembly */
            frame = quicrq_reassembly_frame_create(reassembly_ctx, frame_id);
        }
        /* per fragment logic */
        if (frame == NULL) {
            ret = -1;
        }
        else {
            /* If this is the last fragment, update the frame length */
            if (is_last_fragment) {
                if (frame->final_offset == 0) {
                    frame->final_offset = offset + data_length;
                }
                else if (frame->final_offset != offset + data_length) {
                    ret = -1;
                }
            }
            /* Insert the frame at the proper location */
            ret = quicrq_reassembly_frame_add_packet(frame, current_time, data, offset, data_length);
            if (ret != 0) {
                DBG_PRINTF("Add packet, ret = %d", ret);
            }
            else if (frame->final_offset > 0 && frame->data_received >= frame->final_offset) {
                /* If the frame is complete, verify and submit */
                quicrq_reassembly_frame_mode_enum frame_mode = (reassembly_ctx->next_frame_id == frame_id) ?
                    quicrq_reassembly_frame_in_sequence : quicrq_reassembly_frame_peek;
                if (frame->reassembled == NULL) {
                    /* Reassemble and verify -- maybe should do that in real time instead of at the end? */
                    ret = quicrq_reassembly_frame_reassemble(frame);
                    if (ret == 0) {
                        /* If the frame is fully received, pass it to the application, indicating sequence or not. */
                        ret = ready_fn(app_media_ctx, current_time, frame_id, frame->reassembled, (size_t)frame->final_offset, frame_mode);
                    }
                    if (ret == 0 && frame_mode == quicrq_reassembly_frame_in_sequence) {
                        /* delete the frame that was just reassembled. */
                        quicrq_reassembly_frame_delete(reassembly_ctx, frame);
                        /* update the next_frame id */
                        reassembly_ctx->next_frame_id++;
                        /* try processing all frames that might now be ready */
                        while (ret == 0 && (frame = quicrq_frame_find(reassembly_ctx, reassembly_ctx->next_frame_id)) != NULL && frame->reassembled != NULL) {
                            /* Submit the frame in order */
                            ret = ready_fn(app_media_ctx, current_time, frame->frame_id, frame->reassembled,
                                (size_t)frame->final_offset, quicrq_reassembly_frame_repair);
                            /* delete the frame that was just repaired. */
                            quicrq_reassembly_frame_delete(reassembly_ctx, frame);
                            /* update the next_frame id */
                            reassembly_ctx->next_frame_id++;
                        }
                        /* Mark finished if everything was received */
                        if (reassembly_ctx->final_frame_id > 0 && reassembly_ctx->next_frame_id >= reassembly_ctx->final_frame_id) {
                            reassembly_ctx->is_finished = 1;
                        }
                    }
                }
            }
        }
    }

    return ret;
}

int quicrq_reassembly_learn_final_frame_id(
    quicrq_reassembly_context_t* reassembly_ctx,
    uint64_t final_frame_id)
{
    int ret = 0;

    if (reassembly_ctx->final_frame_id == 0) {
        reassembly_ctx->final_frame_id = final_frame_id;
    }
    else if (final_frame_id != reassembly_ctx->final_frame_id) {
        ret = -1;
    }

    if (ret == 0 && reassembly_ctx->next_frame_id >= final_frame_id) {
        reassembly_ctx->is_finished = 1;
    }

    return ret;
}

uint64_t quicrq_reassembly_frame_id_last(quicrq_reassembly_context_t* reassembly_ctx)
{
    return reassembly_ctx->next_frame_id;
}
