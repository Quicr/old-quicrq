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
  * Manage a list of objects being reassembled. The list is organized as a splay,
  * indexed by the object id and object offset. When a new fragment is received
  * the code will check whether the object is already present, and then whether the
  * fragment for that object has already arrived.
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

typedef struct st_quicrq_reassembly_object_t {
    picosplay_node_t object_node;
    struct st_quicrq_reassembly_packet_t* first_packet;
    struct st_quicrq_reassembly_packet_t* last_packet;
    uint64_t group_id;
    uint64_t object_id;
    uint64_t nb_objects_previous_group;
    uint64_t final_offset;
    uint64_t queue_delay;
    uint8_t flags;
    int is_last_received;
    uint64_t data_received;
    uint64_t last_update_time;
    uint8_t* reassembled;
} quicrq_reassembly_object_t;

/* manage the splay of objects waiting reassembly */

static void* quicrq_object_node_value(picosplay_node_t* object_node)
{
    return (object_node == NULL) ? NULL : (void*)((char*)object_node - offsetof(struct st_quicrq_reassembly_object_t, object_node));
}

static int64_t quicrq_object_node_compare(void* l, void* r) {
    quicrq_reassembly_object_t* left = (quicrq_reassembly_object_t*)l;
    quicrq_reassembly_object_t* right = (quicrq_reassembly_object_t*)r;
    int64_t ret = left->group_id - right->group_id;
    if (ret == 0) {
        ret = left->object_id - right->object_id;
    }
    return ret;
}

static picosplay_node_t* quicrq_object_node_create(void* v_media_object)
{
    return &((quicrq_reassembly_object_t*)v_media_object)->object_node;
}

static void quicrq_object_node_delete(void* tree, picosplay_node_t* node)
{
    if (tree == NULL) {
        DBG_PRINTF("%s", "Attempt to delete from NULL tree");
    }
    memset(node, 0, sizeof(picosplay_node_t));
}

void quicrq_reassembly_init(quicrq_reassembly_context_t* object_list)
{
    picosplay_init_tree(&object_list->object_tree, quicrq_object_node_compare,
        quicrq_object_node_create, quicrq_object_node_delete, quicrq_object_node_value);
}

/* Free the reassembly context
 */
void quicrq_reassembly_release(quicrq_reassembly_context_t* reassembly_ctx)
{
    if (!reassembly_ctx->is_finished) {
        /* Closing before reassembly is finished is most often an error
         * condition. This code tries to provide help when debugging such errors.
         */
        int nb_objects = 0;
        int nb_incomplete = 0;
        picosplay_node_t* next_node = picosplay_first(&reassembly_ctx->object_tree);

        while (next_node != NULL) {
            quicrq_reassembly_object_t* object = (quicrq_reassembly_object_t*)quicrq_object_node_value(next_node);
            nb_objects++;
            if (object->reassembled != NULL) {
                if (nb_incomplete == 0) {
                    DBG_PRINTF("Object %" PRIu64 " is not reassembled", object->object_id);
                }
                nb_incomplete++;
            }
            next_node = picosplay_next(next_node);
        }
        DBG_PRINTF("Reassembly next: %" PRIu64 ", final: %" PRIu64 ", %" PRIu64 ", is_finished: %d",
            reassembly_ctx->next_object_id,
            reassembly_ctx->final_group_id, reassembly_ctx->final_object_id, reassembly_ctx->is_finished);
        DBG_PRINTF("Reassembly contains %d objects, %d incomplete", nb_objects, nb_incomplete);
    }

    picosplay_empty_tree(&reassembly_ctx->object_tree);
    memset(reassembly_ctx, 0, sizeof(quicrq_reassembly_context_t));
}

static quicrq_reassembly_object_t* quicrq_object_find(quicrq_reassembly_context_t* object_list, uint64_t group_id, uint64_t object_id)
{
    quicrq_reassembly_object_t* object = NULL;
    quicrq_reassembly_object_t key_object = { 0 };
    key_object.group_id = group_id;
    key_object.object_id = object_id;
    picosplay_node_t* node = picosplay_find(&object_list->object_tree, (void*)&key_object);
    if (node != NULL) {
        object = (quicrq_reassembly_object_t*)quicrq_object_node_value(node);
    }
    return object;
}

/* Management of the list of objects undergoing reassembly, object-id based logic */
static quicrq_reassembly_packet_t* quicrq_reassembly_object_create_packet(
    quicrq_reassembly_object_t* object,
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
                packet->next_packet = object->first_packet;
                object->first_packet = packet;
            }
            else {
                packet->next_packet = previous_packet->next_packet;
                previous_packet->next_packet = packet;
            }
            if (packet->next_packet == NULL) {
                object->last_packet = packet;
            }
            else {
                packet->next_packet->previous_packet = packet;
            }
            object->data_received += data_length;
            object->last_update_time = current_time;
        }
    }

    return packet;
}

static quicrq_reassembly_object_t* quicrq_reassembly_object_create(quicrq_reassembly_context_t* reassembly_ctx,
    uint64_t group_id, uint64_t object_id)
{
    quicrq_reassembly_object_t* object = (quicrq_reassembly_object_t*)malloc(sizeof(quicrq_reassembly_object_t));
    if (object != NULL) {
        memset(object, 0, sizeof(quicrq_reassembly_object_t));
        object->group_id = group_id;
        object->object_id = object_id;
        picosplay_insert(&reassembly_ctx->object_tree, object);
    }
    return object;
}

static void quicrq_reassembly_object_delete(quicrq_reassembly_context_t* reassembly_ctx, quicrq_reassembly_object_t* object)
{
    /* Free the object's resource */
    quicrq_reassembly_packet_t* packet;

    if (object->reassembled != NULL) {
        free(object->reassembled);
    }

    while ((packet = object->first_packet) != NULL) {
        object->first_packet = packet->next_packet;
        free(packet);
    }

    /* Remove the object from the list */
    picosplay_delete_hint(&reassembly_ctx->object_tree, &object->object_node);

    /* and free the memory */
    free(object);
}

static int quicrq_reassembly_object_add_packet(
    quicrq_reassembly_object_t* object,
    uint64_t current_time,
    const uint8_t* data,
    uint64_t offset,
    size_t data_length)
{
    int ret = 0;
    quicrq_reassembly_packet_t* packet = object->first_packet;
    quicrq_reassembly_packet_t* previous_packet = NULL;

    while (packet != NULL) {
        if (packet->offset >= offset) {
            /* filling a hole */
            if (offset + data_length <= packet->offset) {
                /* No overlap. Just insert the packet after the previous one */
                quicrq_reassembly_packet_t* new_packet = quicrq_reassembly_object_create_packet(object, previous_packet, current_time, data, offset, data_length);
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
                quicrq_reassembly_packet_t* new_packet = quicrq_reassembly_object_create_packet(object, previous_packet, current_time, data, offset, consumed);
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
        quicrq_reassembly_packet_t* new_packet = quicrq_reassembly_object_create_packet(object, previous_packet, current_time, data, offset, data_length);
        if (new_packet == NULL) {
            ret = -1;
        }
        else {
            data_length = 0;
        }
    }

    return ret;
}

static int quicrq_reassembly_object_reassemble(quicrq_reassembly_object_t* object)
{
    int ret = 0;
    /* Special case for zero length objects */
    if (object->is_last_received && object->final_offset == 0 && object->data_received == 0) {
        object->reassembled = (uint8_t*)malloc(1);
        if (object->reassembled == NULL) {
            ret = -1;
        }
    }
    /* Check that that the received bytes are in order */
    else if (object->final_offset == 0 || object->data_received != object->final_offset) {
        ret = -1;
    }
    else if (object->first_packet == NULL || object->first_packet->offset != 0) {
        ret = -1;
    }
    else if (object->last_packet == NULL ||
        object->last_packet->offset + object->last_packet->data_length != object->final_offset) {
        ret = -1;
    }
    else if (object->final_offset > SIZE_MAX) {
        ret = -1;
    }
    else {
        object->reassembled = (uint8_t*)malloc((size_t)object->final_offset);
        if (object->reassembled == NULL) {
            ret = -1;
        }
        else {
            size_t running_offset = 0;
            quicrq_reassembly_packet_t* packet = object->first_packet;
            while (packet != NULL && ret == 0) {
                /* TODO: the "running offset" checks are never supposed to fire, unless
                 * there is a bug in the fragment collection program. Should be removed
                 * once debugging is complete */
                if (packet->offset != running_offset) {
                    ret = -1;
                }
                else if (running_offset + packet->data_length > object->final_offset) {
                    ret = -1;
                }
                else {
                    memcpy(object->reassembled + running_offset, packet->data, packet->data_length);
                    running_offset += packet->data_length;
                    packet = packet->next_packet;
                }
            }
            /* Final check also is just for debugging, should never fire */
            if (ret == 0 && running_offset != object->final_offset) {
                ret = -1;
            }
        }
    }
    return ret;
}

int quicrq_reassembly_update_next_object_id(quicrq_reassembly_context_t* reassembly_ctx,
    uint64_t current_time,
    quicrq_reassembly_object_ready_fn ready_fn,
    void* app_media_ctx)
{
    /* After the next object ID has been updated, deliver the objects that are
     * now "in order" */
    int ret = 0;
    quicrq_reassembly_object_t* object = NULL;

    /* Objects are "in order" if one of two conditions is true:
    *  - The object with key "next group id" and "next object id" is present,
    *  - or, the object with key "next_group_id + 1" and value "next object id=0" is present, and
    *    the number of objects in the previous group matches "next object id"
    */

    while (ret == 0){
        object = quicrq_object_find(reassembly_ctx, reassembly_ctx->next_group_id, reassembly_ctx->next_object_id);
        if (object == NULL) {
            object = quicrq_object_find(reassembly_ctx, reassembly_ctx->next_group_id + 1, 0);
            if (object != NULL && object->reassembled != NULL &&
                object->nb_objects_previous_group == reassembly_ctx->next_object_id) {
                reassembly_ctx->next_group_id += 1;
                reassembly_ctx->next_object_id = 0;
            }
        }
        if (object == NULL || object->reassembled == NULL) {
            break;
        } 
        /* Submit the object in order */
        ret = ready_fn(app_media_ctx, current_time, object->group_id, object->object_id, object->flags, object->reassembled,
            (size_t)object->final_offset, quicrq_reassembly_object_repair);
        /* delete the object that was just repaired. */
        quicrq_reassembly_object_delete(reassembly_ctx, object);
        /* update the next_object id */
        reassembly_ctx->next_object_id++;
    }
    /* Mark finished if everything was received */
    if ((reassembly_ctx->final_group_id > 0 ||reassembly_ctx->final_object_id > 0) &&
        reassembly_ctx->next_group_id >= reassembly_ctx->final_group_id &&
        reassembly_ctx->next_object_id >= reassembly_ctx->final_object_id) {
        reassembly_ctx->is_finished = 1;
    }
    return ret;
}

int quicrq_reassembly_input(
    quicrq_reassembly_context_t* reassembly_ctx,
    uint64_t current_time,
    const uint8_t* data,
    uint64_t group_id,
    uint64_t object_id,
    uint64_t offset,
    uint64_t queue_delay,
    uint8_t flags,
    uint64_t nb_objects_previous_group,
    int is_last_fragment,
    size_t data_length,
    quicrq_reassembly_object_ready_fn ready_fn,
    void* app_media_ctx)
{
    int ret = 0;
    if (group_id < reassembly_ctx->next_group_id ||
        (group_id == reassembly_ctx->next_group_id &&
        object_id < reassembly_ctx->next_object_id)) {
        /* No need for this object. */
    }
    else {
        quicrq_reassembly_object_t* object = quicrq_object_find(reassembly_ctx, group_id, object_id);

        if (object == NULL) {
            /* Create a media object for reassembly */
            object = quicrq_reassembly_object_create(reassembly_ctx, group_id, object_id);
            object->queue_delay = queue_delay;
            object->flags = flags;
        }
        else {
            if (object->queue_delay < queue_delay) {
                object->queue_delay = queue_delay;
            }
        }
        /* per fragment logic */
        if (object == NULL) {
            ret = -1;
        }
        else {
            /* If this is the first fragment and first object, document the previous group */
            if (object_id == 0 && offset == 0) {
                object->nb_objects_previous_group = nb_objects_previous_group;
            }
            /* If this is the last fragment, update the object length */
            if (is_last_fragment) {
                object->is_last_received = 1;
                if (object->final_offset == 0) {
                    object->final_offset = offset + data_length;
                }
                else if (object->final_offset != offset + data_length) {
                    ret = -1;
                }
            }
            /* Insert the object at the proper location */
            ret = quicrq_reassembly_object_add_packet(object, current_time, data, offset, data_length);
            if (ret != 0) {
                DBG_PRINTF("Add packet, ret = %d", ret);
            }
            else if (object->is_last_received && object->data_received >= object->final_offset) {
                /* If the object is complete, verify and submit */
                quicrq_reassembly_object_mode_enum object_mode;
                if (group_id == reassembly_ctx->next_group_id + 1 &&
                    object_id == 0 &&
                    object->nb_objects_previous_group <= reassembly_ctx->next_object_id){
                    /* This is the first object of a new group, and all objects of the previous group
                     * have been received */
                    reassembly_ctx->next_group_id += 1;
                    reassembly_ctx->next_object_id = 0;
                }

                object_mode = (
                    reassembly_ctx->next_group_id == group_id &&
                    reassembly_ctx->next_object_id == object_id ) ?
                    quicrq_reassembly_object_in_sequence : quicrq_reassembly_object_peek;

                if (object->reassembled == NULL) {
                    /* Reassemble and verify -- maybe should do that in real time instead of at the end? */
                    ret = quicrq_reassembly_object_reassemble(object);
                    if (ret == 0) {
                        /* If the object is fully received, pass it to the application, indicating sequence or not. */
                        ret = ready_fn(app_media_ctx, current_time, group_id, object_id, flags, object->reassembled, (size_t)object->final_offset, object_mode);
                    }
                    if (ret == 0 && object_mode == quicrq_reassembly_object_in_sequence) {
                        /* delete the object that was just reassembled. */
                        quicrq_reassembly_object_delete(reassembly_ctx, object);
                        /* update the next_object id */
                        reassembly_ctx->next_object_id++;
                        /* try processing all objects that might now be ready */
                        ret = quicrq_reassembly_update_next_object_id(reassembly_ctx, current_time, ready_fn, app_media_ctx);
                    }
                }
            }
        }
    }

    return ret;
}

int quicrq_reassembly_learn_start_point(
    quicrq_reassembly_context_t* reassembly_ctx,
    uint64_t start_object_id,
    uint64_t current_time,
    quicrq_reassembly_object_ready_fn ready_fn,
    void* app_media_ctx)
{
    int ret = 0;
    if (start_object_id <= reassembly_ctx->next_object_id) {
        /* No need for this object. */
    }
    else {
        /* TODO: more complex if stream can be "repaired" from alternative source */
        /* If packets have been received after that point, they may be considered repaired */
        reassembly_ctx->next_object_id = start_object_id;
        ret = quicrq_reassembly_update_next_object_id(reassembly_ctx, current_time, ready_fn, app_media_ctx);
    }
    return ret;
}

int quicrq_reassembly_learn_final_object_id(
    quicrq_reassembly_context_t* reassembly_ctx,
    uint64_t final_group_id,
    uint64_t final_object_id)
{
    int ret = 0;

    if (reassembly_ctx->final_group_id == 0 && reassembly_ctx->final_object_id == 0) {
        reassembly_ctx->final_group_id = final_group_id;
        reassembly_ctx->final_object_id = final_object_id;
    }
    else if (reassembly_ctx->final_group_id != final_group_id ||
        final_object_id != reassembly_ctx->final_object_id) {
        ret = -1;
    }

    if (ret == 0 && reassembly_ctx->next_object_id >= final_object_id) {
        reassembly_ctx->is_finished = 1;
    }

    return ret;
}

uint64_t quicrq_reassembly_object_id_last(quicrq_reassembly_context_t* reassembly_ctx)
{
    return reassembly_ctx->next_object_id;
}
