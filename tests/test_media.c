
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "quicrq.h"
#include "quicrq_internal.h"
#include "quicrq_tests.h"
#include "quicrq_test_internal.h"
#include "picoquic_utils.h"

/* Unit test of test_media and media api
 */


/* In generation mode, data is created during the test. 
 * In regular mode, data is read from a file.
 * We need to simulate two modes of reading the data, either "streaming"
 * in which the data is sent as fast as the path permits, or "real time"
 * in which the data is only sent if the current time is larger than
 * the creation time.
 */

/* Definition of test publisher. 
 */

void test_media_publisher_close(void* media_ctx)
{
    test_media_publisher_context_t* pub_ctx = (test_media_publisher_context_t*)media_ctx;
    /* Close source file */
    if (pub_ctx->F != NULL) {
        picoquic_file_close(pub_ctx->F);
    }
    if (pub_ctx->generation_context != NULL) {
        free(pub_ctx->generation_context);
    }
    if (pub_ctx->media_frame != NULL) {
        free(pub_ctx->media_frame);
    }
    free(pub_ctx);
}

void* test_media_publisher_init(char const* media_source_path, const generation_parameters_t * generation_model, int is_real_time)
{
    test_media_publisher_context_t* media_ctx = (test_media_publisher_context_t*)
        malloc(sizeof(test_media_publisher_context_t));
    if (media_ctx != NULL) {
        memset(media_ctx, 0, sizeof(test_media_publisher_context_t));
        media_ctx->is_real_time = (is_real_time != 0);
        media_ctx->F = picoquic_file_open(media_source_path, "rb");

        if (media_ctx->F == NULL) {
            if (generation_model != NULL) {
                media_ctx->generation_context = (generation_parameters_t*)malloc(sizeof(generation_parameters_t));
                if (media_ctx->generation_context != NULL) {
                    memcpy(media_ctx->generation_context, generation_model, sizeof(generation_parameters_t));
                }
            }
            if (media_ctx->generation_context == NULL) {
                free(media_ctx);
                media_ctx = NULL;
            }
        }
    }
    return media_ctx;
}

void* test_media_publisher_subscribe(const uint8_t* media_url, const size_t media_url_length, void* v_srce_ctx)
{
    test_media_source_context_t* srce_ctx = (test_media_source_context_t*)v_srce_ctx;
    test_media_publisher_context_t* media_ctx = test_media_publisher_init(srce_ctx->file_path, srce_ctx->generation_context, srce_ctx->is_real_time);

    if (media_ctx != NULL) {
        media_ctx->p_next_time = srce_ctx->p_next_time;
    }

    return media_ctx;
}

/* Media publisher callback for stream mode.
 * In stream mode, the frame data is directly copied to the output.
 */

int test_media_allocate_frame(test_media_publisher_context_t* pub_ctx, size_t target_size)
{
    int ret = 0;

    if (pub_ctx->media_frame_size > target_size) {
        ret = -1;
    } else if (pub_ctx->media_frame_alloc < target_size) {
        uint8_t* new_memory = (uint8_t*)malloc(target_size);
        if (new_memory == NULL){
            ret = -1;
        }
        else {
            if (pub_ctx->media_frame_size > 0 && pub_ctx->media_frame_size <= target_size) {
                memcpy(new_memory, pub_ctx->media_frame, pub_ctx->media_frame_size);
            }
            if (pub_ctx->media_frame_alloc > 0) {
                free(pub_ctx->media_frame);
            }
            pub_ctx->media_frame_alloc = target_size;
            pub_ctx->media_frame = new_memory;
        }
    }
    return ret;
}

int test_media_read_frame_from_file(test_media_publisher_context_t* pub_ctx)
{
    /* If there is no memory, allocate default size. */
    size_t nb_read;
    pub_ctx->media_frame_size = 0;
    int ret = test_media_allocate_frame(pub_ctx, QUIRRQ_MEDIA_TEST_HEADER_SIZE);
    if (ret == 0) {
        /* Read the frame header */
        nb_read = fread(pub_ctx->media_frame, 1, QUIRRQ_MEDIA_TEST_HEADER_SIZE, pub_ctx->F);
        if (nb_read != QUIRRQ_MEDIA_TEST_HEADER_SIZE) {
            /* Assume this is the end of file. */
            pub_ctx->is_finished = 1;
        }
        else {
            /* decode the frame header */
            const uint8_t* fh_max = pub_ctx->media_frame + QUIRRQ_MEDIA_TEST_HEADER_SIZE;
            const uint8_t* fh = quicr_decode_frame_header(pub_ctx->media_frame, fh_max, &pub_ctx->current_header);

            if (fh != NULL){
                /* If there is not enough memory, allocate data for a full frame */
                size_t target_size = (fh - pub_ctx->media_frame) + pub_ctx->current_header.length;
                pub_ctx->media_frame_size = QUIRRQ_MEDIA_TEST_HEADER_SIZE;
                ret = test_media_allocate_frame(pub_ctx, target_size);
                if (ret == 0){
                    /* Read the frame content */
                    size_t required = target_size - pub_ctx->media_frame_size;
                    nb_read = fread(pub_ctx->media_frame + pub_ctx->media_frame_size, 1, required, pub_ctx->F);
                    if (nb_read != required) {
                        ret = -1;
                        DBG_PRINTF("Reading %zu frame bytes, required %zu, ret=%d", nb_read, required, ret);
                    }
                    else {
                        pub_ctx->media_frame_size = target_size;
                    }
                }
            }
            else {
                /* malformed header ! */
                ret = -1;
                DBG_PRINTF("Reading malformed frame header, ret=%d", ret);
            }       
        }
    }
    return ret;
}

size_t test_media_generate_frame_size(generation_parameters_t * gen_ctx)
{
    size_t l = 0;
    size_t size_min = gen_ctx->target_p_min;
    size_t size_max = gen_ctx->target_p_max;
    size_t delta = 0;
    size_t multiply = 1;
    size_t reminder = 0;
    size_t r_delta = 0;
    /* Is this an I frame? If yes, size_min and size_max are bigger */
    if (gen_ctx->frames_in_epoch > 0 && (gen_ctx->nb_frames_sent% gen_ctx->frames_in_epoch) == 0) {
        size_min *= gen_ctx->nb_p_in_i;
        size_max *= gen_ctx->nb_p_in_i;
    }
    /* Do a random allocation */
    l = size_min;
    delta = size_max - size_min;
    if (delta > 0) {
        if (delta > RAND_MAX) {
            multiply = (delta + RAND_MAX - 1) / RAND_MAX;
        }
        reminder = (multiply * RAND_MAX) % delta;
        while (r_delta < reminder) {
            r_delta = multiply * rand();
        }
        l += (r_delta - reminder) % delta;
    }
    return l;
}

int test_media_generate_frame(test_media_publisher_context_t* pub_ctx)
{
    int ret = 0;

    /* Compute the time stamp.
        * At this point, we have a conflict between the way time in microseconds
        * per QUIC API, and specifications like "30 fps" that do not result in
        * an integer number of microseconds. We deal with that by computing a
        * virtual time as "number of elapsed frames * 1,000,000 / fps ".
        * 
        */
    pub_ctx->current_header.number = pub_ctx->generation_context->nb_frames_sent;
    pub_ctx->current_header.timestamp = 
        (pub_ctx->generation_context->nb_frames_elapsed * 1000000ull) /
        pub_ctx->generation_context->frames_per_second;
    if (pub_ctx->current_header.timestamp >= pub_ctx->generation_context->target_duration) {
        /* No frame to generate, same as end of file */
        pub_ctx->is_finished = 1;
    }
    else {
        size_t frame_size_max;

        /* Compute the content size */
        pub_ctx->current_header.length = test_media_generate_frame_size(pub_ctx->generation_context);
        frame_size_max = pub_ctx->current_header.length + QUIRRQ_MEDIA_TEST_HEADER_SIZE;
        ret = test_media_allocate_frame(pub_ctx, frame_size_max);
        if (ret == 0) {
            const uint8_t* fh;
            /* Generate the frame header */
            fh = quicr_encode_frame_header(pub_ctx->media_frame, pub_ctx->media_frame + QUIRRQ_MEDIA_TEST_HEADER_SIZE, &pub_ctx->current_header);
            if (fh == NULL) {
                ret = -1;
            }
            else {
                /* Generate the frame content */
                uint8_t* content = pub_ctx->media_frame + (fh - pub_ctx->media_frame);

                for (size_t i = 0; i < pub_ctx->current_header.length; i++) {
                    content[i] = (uint8_t)(rand() & 0xff);
                }
                pub_ctx->media_frame_size = (fh - pub_ctx->media_frame) + pub_ctx->current_header.length;
                /* Update the generation context */
                pub_ctx->generation_context->nb_frames_elapsed += 1;
                pub_ctx->generation_context->nb_frames_sent += 1;
            }
        }
    }
    return ret;
}

static int test_media_publisher_check_frame(test_media_publisher_context_t* pub_ctx)
{
    int ret = 0;

    if (pub_ctx->media_frame_size <= pub_ctx->media_frame_read) {
        /* No more frame data available. */
        pub_ctx->media_frame_size = 0;
        pub_ctx->media_frame_read = 0;
        if (pub_ctx->F != NULL) {
            /* Read the next frame from the file */
            ret = test_media_read_frame_from_file(pub_ctx);
        }
        else {
            /* Generate a frame */
            ret = test_media_generate_frame(pub_ctx);
        }
    }

    return ret;
}

int test_media_publisher_fn(
    quicrq_media_source_action_enum action,
    void* media_ctx,
    uint8_t* data,
    size_t data_max_size,
    size_t* data_length,
    int * is_finished,
    uint64_t current_time)
{
    int ret = 0;
    test_media_publisher_context_t* pub_ctx = (test_media_publisher_context_t*)media_ctx;

    if (action == quicrq_media_source_get_data) {

        ret = test_media_publisher_check_frame(pub_ctx);

        if (ret == 0) {
            if (pub_ctx->is_finished) {
                *is_finished = 1;
                *data_length = 0;
            }
            else if (pub_ctx->media_frame_size > pub_ctx->media_frame_read){
                if (!pub_ctx->is_real_time || 
                    current_time >= pub_ctx->current_header.timestamp) {
                    /* Copy data from frame in memory */
                    size_t available = pub_ctx->media_frame_size - pub_ctx->media_frame_read;
                    size_t copied = (available > data_max_size) ? data_max_size : available;
                    *data_length = copied;
                    if (data != NULL) {
                        /* If data is set to NULL, return the available size but do not copy anything */
                        memcpy(data, pub_ctx->media_frame + pub_ctx->media_frame_read, copied);
                        pub_ctx->media_frame_read += copied;
                    }
                    *pub_ctx->p_next_time = UINT64_MAX;
                }
                else {
                    *pub_ctx->p_next_time = pub_ctx->current_header.timestamp;
                    *data_length = 0;
                }
            }
            else
            {
                *data_length = 0;
            }
        }
    }
    else if (action == quicrq_media_source_close) {
        /* close the context */
        test_media_publisher_close(media_ctx);
    }
    return ret;
}

uint64_t test_media_publisher_next_time(void * media_ctx, uint64_t current_time)
{
    test_media_publisher_context_t* pub_ctx = (test_media_publisher_context_t*)media_ctx;
    uint64_t next_time = current_time;
    int ret = test_media_publisher_check_frame(pub_ctx);

    if (ret == 0 && pub_ctx->current_header.timestamp > next_time) {
        next_time = pub_ctx->current_header.timestamp;
    }

    return next_time;
}

/* Provide an API for "declaring" a test media to the local quicrq context  */
static test_media_source_context_t* test_media_create_source(char const* media_source_path, const generation_parameters_t* generation_model, int is_real_time, uint64_t* p_next_time)
{
    test_media_source_context_t* srce_ctx = (test_media_source_context_t*)malloc(sizeof(test_media_source_context_t));

    if (srce_ctx != NULL) {
        memset(srce_ctx, 0, sizeof(test_media_source_context_t));
        srce_ctx->file_path = media_source_path;
        srce_ctx->generation_context = generation_model;
        srce_ctx->is_real_time = is_real_time;
        srce_ctx->p_next_time = p_next_time;
        *srce_ctx->p_next_time = UINT64_MAX;
    }
    return srce_ctx;
}


int test_media_publish(quicrq_ctx_t * qr_ctx, uint8_t* url, size_t url_length, char const* media_source_path, const generation_parameters_t* generation_model, int is_real_time, uint64_t * p_next_time)
{
    int ret = 0; 
    test_media_source_context_t* srce_ctx = test_media_create_source(media_source_path, generation_model, is_real_time, p_next_time);

    if (srce_ctx == NULL) {
        ret = -1;
    }
    else {
        ret = quicrq_publish_source(qr_ctx, url, url_length, srce_ctx, test_media_publisher_subscribe, test_media_publisher_fn);
    }
    return ret;
}

/* Media receiver definitions */
typedef struct st_test_media_consumer_packet_t {
    struct st_test_media_consumer_packet_t* next_packet;
    struct st_test_media_consumer_packet_t* previous_packet;
    uint64_t current_time;
    uint8_t* data;
    uint64_t offset;
    size_t data_length;
} test_media_consumer_packet_t;


typedef struct st_test_media_consumer_context_t {
    FILE* Res;
    FILE* Log;
    uint8_t header_bytes[QUIRRQ_MEDIA_TEST_HEADER_SIZE];
    quicrq_media_frame_header_t current_header;
    size_t media_frame_received;
    size_t target_size;
    uint64_t highest_offset;
    uint64_t final_offset;
    struct st_test_media_consumer_packet_t* first_packet;
    struct st_test_media_consumer_packet_t* last_packet;
    unsigned int is_finished : 1;
    unsigned int header_received : 1;
} test_media_consumer_context_t;

int test_media_consumer_close(void* media_ctx)
{
    /* Close result file and log file */
    int ret = 0;
    test_media_consumer_context_t* cons_ctx = (test_media_consumer_context_t*)media_ctx;
    test_media_consumer_packet_t* packet;

    if ((packet = cons_ctx->first_packet) != NULL) {
        ret = -1;
        DBG_PRINTF("Closing consumer with unprocessed frame, %llu, ret=%d", (unsigned long long)packet->offset, ret);
    }

    while ((packet = cons_ctx->first_packet) != NULL) {
        cons_ctx->first_packet = packet->next_packet;
        free(packet);
    }

    if (cons_ctx->Res != NULL) {
        picoquic_file_close(cons_ctx->Res);
    }
    if (cons_ctx->Log != NULL) {
        picoquic_file_close(cons_ctx->Log);
    }
    free(media_ctx);

    return ret;
}

void* test_media_consumer_init(char const* media_result_file, char const * media_result_log)
{
    /* Open and initialize result file and log file */
    test_media_consumer_context_t * cons_ctx = (test_media_consumer_context_t*)malloc(sizeof(test_media_consumer_context_t));
    if (cons_ctx != NULL) {
        memset(cons_ctx, 0, sizeof(test_media_consumer_context_t));
        cons_ctx->Res = picoquic_file_open(media_result_file, "wb");
        cons_ctx->Log = picoquic_file_open(media_result_log, "w");
        if (cons_ctx->Res == NULL || cons_ctx->Log == NULL) {
            (void)test_media_consumer_close(cons_ctx);
            cons_ctx = NULL;
        }
    }
    return cons_ctx;
}

/* Learn the final offset of a stream.
 * This is an opportunity to discover gaps.
 */
int test_media_consumer_learn_final_offset(
    void* media_ctx,
    uint64_t offset)
{
    int ret = 0;
    test_media_consumer_context_t* cons_ctx = (test_media_consumer_context_t*)media_ctx;

    if (cons_ctx->final_offset == 0) {
        cons_ctx->final_offset = offset;
    }
    else if (offset != cons_ctx->final_offset) {
        ret = -1;
    }

    if (ret == 0 && cons_ctx->highest_offset >= offset) {
        cons_ctx->is_finished = 1;
    }

    return ret;
}

static test_media_consumer_packet_t* test_media_store_create_packet(
    test_media_consumer_context_t* cons_ctx,
    test_media_consumer_packet_t* previous_packet,
    uint64_t current_time,
    const uint8_t* data,
    uint64_t offset,
    size_t data_length)
{
    test_media_consumer_packet_t* packet = NULL;
    size_t packet_size = sizeof(test_media_consumer_packet_t) + data_length;
    if (packet_size >= sizeof(test_media_consumer_packet_t)) {
        packet = (test_media_consumer_packet_t*)malloc(packet_size);
        if (packet != NULL) {
            memset(packet, 0, sizeof(test_media_consumer_packet_t));
            packet->current_time = current_time;
            packet->offset = offset;
            packet->data_length = data_length;
            packet->data = ((uint8_t*)packet) + sizeof(test_media_consumer_packet_t);
            memcpy(packet->data, data, data_length);

            packet->previous_packet = previous_packet;
            if (previous_packet == NULL) {
                packet->next_packet = cons_ctx->first_packet;
                cons_ctx->first_packet = packet;
            }
            else {
                packet->next_packet = previous_packet->next_packet;
                previous_packet->next_packet = packet;
            }
            if (packet->next_packet == NULL) {
                cons_ctx->last_packet = packet;
            }
            else {
                packet->next_packet->previous_packet = packet;
            }
        }
    }

    return packet;
}

int test_media_consumer_data_ready(
    void* media_ctx,
    uint64_t current_time,
    const uint8_t* data,
    uint64_t offset,
    size_t data_length)
{
    int ret = 0;
    size_t processed = 0;
    test_media_consumer_context_t* cons_ctx = (test_media_consumer_context_t*)media_ctx;
    cons_ctx->highest_offset += data_length;

    /* While some bytes to process, process them. Call may straddle several media frames */
    while (processed < data_length) {
        size_t available = data_length - processed;
        if (!cons_ctx->header_received) {
            size_t h_bytes = QUIRRQ_MEDIA_TEST_HEADER_SIZE - cons_ctx->media_frame_received;
            if (h_bytes > available) {
                h_bytes = available;
            }
            memcpy(cons_ctx->header_bytes + cons_ctx->media_frame_received, data + processed, h_bytes);
            processed += h_bytes;
            cons_ctx->media_frame_received += h_bytes;
            if (cons_ctx->media_frame_received >= QUIRRQ_MEDIA_TEST_HEADER_SIZE) {
                const uint8_t* fh = quicr_decode_frame_header(cons_ctx->header_bytes,
                    cons_ctx->header_bytes + QUIRRQ_MEDIA_TEST_HEADER_SIZE, &cons_ctx->current_header);
                if (fh == NULL) {
                    ret = -1;
                    break;
                }
                else {
                    cons_ctx->header_received = 1;
                    cons_ctx->target_size = (fh - cons_ctx->header_bytes) + cons_ctx->current_header.length;
                    if (fwrite(cons_ctx->header_bytes, 1, cons_ctx->media_frame_received, cons_ctx->Res) != cons_ctx->media_frame_received) {
                        ret = -1;
                        break;
                    }
                }
            }
        }
        else {
            size_t required = cons_ctx->target_size - cons_ctx->media_frame_received;
            size_t copied = (required > available) ? available : required;

            if (fwrite(data + processed, 1, copied, cons_ctx->Res) != copied) {
                ret = -1;
                break;
            }
            else {
                cons_ctx->media_frame_received += copied;
                processed += copied;
            }
            if (cons_ctx->media_frame_received >= cons_ctx->target_size) {
                /* Frame was finally received */
                if (fprintf(cons_ctx->Log, "%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%zu\n",
                    current_time, cons_ctx->current_header.timestamp, cons_ctx->current_header.number, cons_ctx->current_header.length) <= 0) {
                    ret = -1;
                }
                else {
                    /* Prepare to receive the next frame. */
                    cons_ctx->media_frame_received = 0;
                    cons_ctx->header_received = 0;
                }
            }
        }
    }

    if (ret == 0 && cons_ctx->final_offset > 0 && cons_ctx->highest_offset >= cons_ctx->final_offset) {
        cons_ctx->is_finished = 1;
    }

    return ret;
}

int test_media_consumer_datagram_ready(
    void* media_ctx,
    uint64_t current_time,
    const uint8_t* data,
    uint64_t offset,
    size_t data_length)
{
    int ret = 0;
    test_media_consumer_context_t* cons_ctx = (test_media_consumer_context_t*)media_ctx;
    /* is this datagram in order? */
    if (offset + data_length < cons_ctx->highest_offset) {
        /* Redundant replica of an old packet. Nothing to see. */
        ret = 0;
    }
    else if (cons_ctx->first_packet == NULL && offset <= cons_ctx->highest_offset) {
        /* Everything in order so far, just submit the data */
        size_t consumed = cons_ctx->highest_offset - offset;
        data += consumed;
        offset += consumed;
        data_length -= consumed;
        ret = test_media_consumer_data_ready(media_ctx, current_time, data, offset, data_length);
    }
    else if (cons_ctx->last_packet == NULL || cons_ctx->last_packet->offset <= offset) {
        /* Short cut if this is the last packet -- just add at the tail of the list */
        test_media_consumer_packet_t* new_packet = test_media_store_create_packet(cons_ctx, cons_ctx->last_packet, current_time, data, offset, data_length);
        if (new_packet == NULL) {
            ret = -1;
        }
    }
    else {
        /* It is not in order. Insert ranked by offset */
        test_media_consumer_packet_t* packet = cons_ctx->first_packet;
        test_media_consumer_packet_t* previous_packet = NULL;

        while (packet != NULL) {
            if (packet->offset >= offset) {
                /* filling a hole */
                if (offset + data_length <= packet->offset) {
                    /* No overlap. Just insert the packet after the previous one */
                    test_media_consumer_packet_t* new_packet = test_media_store_create_packet(cons_ctx, previous_packet, current_time, data, offset, data_length);
                    if (new_packet == NULL) {
                        ret = -1;
                    }
                    else {
                        data_length = 0;
                    }
                    break;
                }
                else {
                    if (offset < packet->offset) {
                        /* partial overlap. Create a packet for the non overlapping part, then retain the bytes at the end. */
                        test_media_consumer_packet_t* new_packet = test_media_store_create_packet(cons_ctx, previous_packet, current_time, data, offset, packet->offset - offset);
                        if (new_packet == NULL) {
                            ret = -1;
                            break;
                        }
                        else {
                            /* Trim the data. First remove the part that was consumed */
                            size_t consumed = cons_ctx->highest_offset - offset;
                            data += consumed;
                            offset += consumed;
                            data_length -= consumed;
                        }
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
                    size_t consumed = packet->offset + packet->data_length - offset;
                    data += consumed;
                    offset += consumed;
                    data_length -= consumed;
                }
            }
            else {
                /* after next packet, no overlap, need to continue the chain */
                previous_packet = packet;
                packet = packet->next_packet;
            }
        }
        /* All packets in store have been checked */
        if (ret == 0 && data_length > 0) {
            /* Some of the incoming data was not inserted */
            test_media_consumer_packet_t* new_packet = test_media_store_create_packet(cons_ctx, previous_packet, current_time, data, offset, data_length);
            if (new_packet == NULL) {
                ret = -1;
            }
            else {
                data_length = 0;
            }
        }

        /* Something has changed. Check whether the first hole has been filled and packets can be delivered */
        if (ret == 0) {
            packet = cons_ctx->first_packet;

            while (ret == 0 && packet != NULL && packet->offset == cons_ctx->highest_offset) {
                /* The first packet in the list fills the first hole. Submit and then delete. */
                ret = test_media_consumer_data_ready(media_ctx, packet->current_time, packet->data, packet->offset, packet->data_length);
                cons_ctx->first_packet = packet->next_packet;
                if (packet->next_packet == NULL) {
                    cons_ctx->first_packet = NULL;
                    cons_ctx->last_packet = NULL;
                }
                else {
                    cons_ctx->first_packet = packet->next_packet;
                    packet->next_packet->previous_packet = NULL;
                }
                free(packet);
                packet = cons_ctx->first_packet;
            }
        }
    }

    return ret;
}

int test_media_consumer_cb(
    quicrq_media_consumer_enum action,
    void* media_ctx,
    uint64_t current_time,
    const uint8_t* data,
    uint64_t offset,
    size_t data_length)
{
    int ret = 0;
    switch (action) {
    case quicrq_media_data_ready:
        ret = test_media_consumer_data_ready(media_ctx, current_time, data, offset, data_length);
        break;
    case quicrq_media_datagram_ready:
        ret = test_media_consumer_datagram_ready(media_ctx, current_time, data, offset, data_length);
        break;
    case quicrq_media_final_offset:
        test_media_consumer_learn_final_offset(media_ctx, offset);
        break;
    case quicrq_media_close:
        ret = test_media_consumer_close(media_ctx);
        break;
    default:
        ret = -1;
        break;
    }
    return ret;
}

int test_media_subscribe(quicrq_cnx_ctx_t* cnx_ctx, uint8_t* url, size_t url_length, int use_datagrams, char const* media_result_file, char const* media_result_log)
{
    int ret = 0;
    void* media_ctx = test_media_consumer_init(media_result_file, media_result_log);

    if (media_ctx == NULL) {
        ret = -1;
    }
    else {
        ret = quicrq_cnx_subscribe_media(cnx_ctx, url, url_length, use_datagrams, test_media_consumer_cb, media_ctx);
    }

    return ret;
}

/* Compare media file.
 * These are binary files composed of sequences of frames.
 */
int quicrq_compare_media_file(char const* media_result_file, char const* media_reference_file)
{
    int ret = 0;
    /* Open contexts for each file */
    test_media_publisher_context_t* result_ctx = (test_media_publisher_context_t*)test_media_publisher_init(media_result_file, NULL, 0);
    test_media_publisher_context_t* ref_ctx = (test_media_publisher_context_t*)test_media_publisher_init(media_reference_file, NULL, 0);

    if (result_ctx == NULL || ref_ctx == NULL) {
        ret = -1;
        DBG_PRINTF("Could not create result(0x%x) or reference(0x%x) publisher contexts, ret=%d", result_ctx, ref_ctx, ret);
    }
    else {
        /* Read the frames on both. They should match, or both should come to an end */
        while (ret == 0 && !result_ctx->is_finished && !ref_ctx->is_finished) {
            ret = test_media_read_frame_from_file(result_ctx);
            if (ret != 0) {
                DBG_PRINTF("Could not read frame from results, ret=%d", ret);
            } else {
                ret = test_media_read_frame_from_file(ref_ctx);
                if (ret == 0) {
                    /* Compare the media frames */
                    if (result_ctx->is_finished) {
                        if (!ref_ctx->is_finished) {
                            ret = -1;
                            DBG_PRINTF("Result file finished before reference: ret=%d", ret);
                        }
                    }
                    else if (ref_ctx->is_finished) {
                        if (!result_ctx->is_finished) {
                            ret = -1;
                            DBG_PRINTF("Result file not finished with reference: ret=%d", ret);
                        }
                    }
                    else if (ref_ctx->current_header.timestamp != result_ctx->current_header.timestamp) {
                        ret = -1;
                        DBG_PRINTF("Time stamps differ, %llx vs %llx: ret=%d", (unsigned long long)ref_ctx->current_header.timestamp, 
                            (unsigned long long)result_ctx->current_header.timestamp, ret);
                    }
                    else if (ref_ctx->current_header.number != result_ctx->current_header.number) {
                        ret = -1;
                        DBG_PRINTF("Numbers differ, %llu vs %llu: ret=%d", (unsigned long long)ref_ctx->current_header.number,
                            (unsigned long long)result_ctx->current_header.number, ret);
                    }
                    else if (ref_ctx->current_header.length != result_ctx->current_header.length) {
                        ret = -1;
                        DBG_PRINTF("Lengths differ, %lu vs %lu: ret=%d", (unsigned long)ref_ctx->current_header.number,
                            (unsigned long)result_ctx->current_header.number, ret);
                    }
                    else if (ref_ctx->media_frame_size != result_ctx->media_frame_size){
                        ret = -1;
                        DBG_PRINTF("Frame sizes differ, %zu vs %zu: ret=%d",  ref_ctx->media_frame_size,
                            result_ctx->media_frame_size, ret);
                    }
                    else if (memcmp(ref_ctx->media_frame, result_ctx->media_frame, ref_ctx->media_frame_size) != 0) {
                        ret = -1;
                        DBG_PRINTF("Frame contents differ: ret=%d", ret);
                    }
                }
            }
        }
    }
    if (result_ctx != NULL) {
        test_media_publisher_close(result_ctx);
    }
    if (ref_ctx != NULL) {
        test_media_publisher_close(ref_ctx);
    }

    return ret;
}

/* Compare log file to reference log file  
 */
int quicrq_compare_log_file(char const* media_result_log, char const* media_reference_log)
{
    int ret = 0;
    int last_err1=0;
    int last_err2 = 0;
    FILE* F = picoquic_file_open_ex(media_result_log, "r", &last_err1);
    FILE* G = picoquic_file_open_ex(media_reference_log, "r", &last_err2);

    if (F == NULL || G == NULL) {
        ret = -1;
    }
    else {
        char result_line[512];
        char ref_line[512];
        while (ret == 0) {
            char* result_read = fgets(result_line, sizeof(result_line), F);
            char* ref_read = fgets(ref_line, sizeof(ref_line), G);
            if (result_read == NULL) {
                if (ref_read != NULL) {
                    ret = -1;
                }
                break;
            } else if (ref_read == NULL) {
                if (result_read != NULL) {
                    ret = -1;
                }
                break;
            }
            else if (strcmp(ref_read, result_read) != 0) {
                ret = -1;
            }
        }
    }
    if (F != NULL) {
        picoquic_file_close(F);
    }
    if (G != NULL) {
        picoquic_file_close(G);
    }
    return ret;
}

/* The media test provides two results:
 * - a media result file, which shuld be identical to the media source file
 * - a media result log, which provides for each received frame the receive time, compared to the media time
 */

int quicrq_media_api_test_one(char const *media_source_name, char const* media_log_reference, 
    char const * media_result_file, char const* media_result_log, const generation_parameters_t* generation_model, int is_real_time)
{
    int ret = 0;
    char media_source_path[512];
    char media_log_ref_path[512];
    uint8_t media_buffer[1024];
    uint64_t current_time = 0;
    uint64_t next_time;
    uint64_t published_offset = 0;
    size_t data_length;
    void* srce_ctx = NULL;
    void* pub_ctx = NULL;
    void* cons_ctx = NULL;
    int is_finished = 0;
    int inactive = 0;

    /* Locate the source and reference file */
    if (picoquic_get_input_path(media_source_path, sizeof(media_source_path),
        quicrq_test_solution_dir, media_source_name) != 0 ||
        picoquic_get_input_path(media_log_ref_path, sizeof(media_log_ref_path),
            quicrq_test_solution_dir, media_log_reference) != 0){
        ret = -1;
    }

    /* Init the publisher and consumer */
    if (ret == 0) {
        srce_ctx = test_media_create_source(media_source_path, generation_model, is_real_time, &next_time);
        if (srce_ctx != NULL) {
            pub_ctx = test_media_publisher_subscribe((uint8_t*)media_source_path, strlen(media_source_path), srce_ctx);
        }
        cons_ctx = test_media_consumer_init(media_result_file, media_result_log);
        if (pub_ctx == NULL || cons_ctx == NULL){
            ret = -1;
        }
    }

    /* Loop through publish and consume until finished */
    while (ret == 0 && !is_finished && inactive < 32) {
        ret = test_media_publisher_fn(quicrq_media_source_get_data,
            pub_ctx, media_buffer, sizeof(media_buffer),
            &data_length, &is_finished, current_time);
        if (ret == 0 && !is_finished && data_length == 0) {
            /* Update the current time to reflect media time */
            current_time = test_media_publisher_next_time(pub_ctx, current_time);
            inactive++;
        } else if (ret == 0) {
            inactive = 0;
            ret = test_media_consumer_cb(quicrq_media_data_ready, cons_ctx, current_time, media_buffer,
                published_offset, data_length);
            published_offset += data_length;
        }
    }

    /* Close publisher and consumer */
    if (pub_ctx != NULL) {
        test_media_publisher_close(pub_ctx);
    }

    if (cons_ctx != NULL) {
        int close_ret = test_media_consumer_close(cons_ctx);
        if (ret == 0) {
            ret = close_ret;
        }
    }

    /* Compare media result to media source */
    if (ret == 0) {
        ret = quicrq_compare_log_file(media_result_log, media_log_ref_path);
    }

    if (ret == 0) {
        ret = quicrq_compare_media_file(media_result_file, media_source_path);
    }

    return ret;
}

#ifdef _WINDOWS
#define QUICRQ_TEST_VIDEO1_SOURCE "tests\\video1_source.bin"
#define QUICRQ_TEST_VIDEO1_LOGREF "tests\\video1_logref.csv"
#define QUICRQ_TEST_VIDEO1_RT_LOGREF "tests\\video1_rt_logref.csv"
#else
#define QUICRQ_TEST_VIDEO1_SOURCE "tests/video1_source.bin"
#define QUICRQ_TEST_VIDEO1_LOGREF "tests/video1_logref.csv"
#define QUICRQ_TEST_VIDEO1_RT_LOGREF "tests/video1_rt_logref.csv"
#endif
#define QUICRQ_TEST_VIDEO1_RESULT "video1_result.bin"
#define QUICRQ_TEST_VIDEO1_LOG    "video1_log.csv"
#define QUICRQ_TEST_VIDEO1_RT_RESULT "video1_rt_result.bin"
#define QUICRQ_TEST_VIDEO1_RT_LOG    "video1_rt_log.csv"
#define QUICRQ_TEST_VIDEO1_LOSS_RESULT "video1_loss_result.bin"
#define QUICRQ_TEST_VIDEO1_LOSS_LOG    "video1_loss_log.csv"


const generation_parameters_t video_1mps = {
    10000000, 30, 10, 60, 4000, 5000, 0, 0 };

int quicrq_media_video1_test()
{
    int ret = quicrq_media_api_test_one(QUICRQ_TEST_VIDEO1_SOURCE, QUICRQ_TEST_VIDEO1_LOGREF, QUICRQ_TEST_VIDEO1_RESULT, QUICRQ_TEST_VIDEO1_LOG,
        &video_1mps, 0);

    return ret;
}

int quicrq_media_video1_rt_test()
{
    int ret = quicrq_media_api_test_one(QUICRQ_TEST_VIDEO1_SOURCE, QUICRQ_TEST_VIDEO1_RT_LOGREF, 
        QUICRQ_TEST_VIDEO1_RT_RESULT, QUICRQ_TEST_VIDEO1_RT_LOG,
        &video_1mps, 1);

    return ret;
}

/* Verify that a media file can be obtained through the local publish API
 */

int quicrq_media_publish_test_one(char const* media_source_name, char const* media_log_reference,
    char const* media_result_file, char const* media_result_log, const generation_parameters_t* generation_model, int is_real_time)
{
    int ret = 0;
    char media_source_path[512];
    char media_log_ref_path[512];
    uint8_t media_buffer[1024];
    uint64_t current_time = 0;
    uint64_t published_offset = 0;
    size_t data_length;
    void* cons_ctx = NULL;
    int is_finished = 0;
    uint64_t simulated_time = 0;
    uint64_t media_next_time = 0;
    quicrq_cnx_ctx_t* cnx_ctx = NULL;
    quicrq_stream_ctx_t* stream_ctx = NULL;
    quicrq_ctx_t* qr_ctx = quicrq_create(NULL,
        NULL, NULL, NULL, NULL, NULL,
        NULL, 0, &simulated_time);
    int inactive = 0;

    /* Create empty contexts for qr object, connection, stream */
    if (qr_ctx == NULL) {
        ret = -1;
    }
    else {
        struct sockaddr_in addr_to = { 0 };
        picoquic_cnx_t * cnx = picoquic_create_cnx(quicrq_get_quic_ctx(qr_ctx), picoquic_null_connection_id, picoquic_null_connection_id,
            (struct sockaddr*)&addr_to, simulated_time, 0, NULL, QUICRQ_ALPN, 1);
        cnx_ctx = quicrq_create_cnx_context(qr_ctx, cnx);
        if (cnx_ctx == NULL) {
            ret = -1;
        }
        else {
            stream_ctx = quicrq_create_stream_context(cnx_ctx, 0);
            if (stream_ctx == NULL) {
                ret = -1;
            }
        }
    }

    /* Locate the source and reference file */
    if (picoquic_get_input_path(media_source_path, sizeof(media_source_path),
        quicrq_test_solution_dir, media_source_name) != 0 ||
        picoquic_get_input_path(media_log_ref_path, sizeof(media_log_ref_path),
            quicrq_test_solution_dir, media_log_reference) != 0) {
        ret = -1;
    }


    /* Publish a test file */
    if (ret == 0){
        ret = test_media_publish(qr_ctx, (uint8_t*)media_source_name, strlen(media_source_name), media_source_path, generation_model, is_real_time, &media_next_time);
    }

    /* Connect the stream context to the publisher */
    if (ret == 0) {
        ret = quicrq_subscribe_local_media(stream_ctx, (uint8_t*)media_source_name, strlen(media_source_name));
    }
    /* Initialize a consumer context for testing */
    if (ret == 0) {
        cons_ctx = test_media_consumer_init(media_result_file, media_result_log);
        if (cons_ctx == NULL) {
            ret = -1;
        }
    }

    /* Loop through publish and consume until finished */
    while (ret == 0 && !is_finished && inactive < 32) {
        ret = stream_ctx->publisher_fn(quicrq_media_source_get_data,
            stream_ctx->media_ctx, media_buffer, sizeof(media_buffer),
            &data_length, &is_finished, current_time);
        if (ret == 0) {
            if (is_finished || data_length > 0) {
                ret = test_media_consumer_cb(quicrq_media_data_ready, cons_ctx, current_time, media_buffer,
                    published_offset, data_length);
                published_offset += data_length;
                inactive = 0;
            }
            else {
                current_time = test_media_publisher_next_time(stream_ctx->media_ctx, current_time);
                inactive++;
            }
        }
    }

    /* Close publisher by closing the connection context */
    if (ret == 0) {
        quicrq_delete_cnx_context(cnx_ctx);
    }
    /* Close consumer */
    if (cons_ctx != NULL) {
        int close_ret = test_media_consumer_close(cons_ctx);
        if (ret == 0) {
            ret = close_ret;
        }
    }

    /* Compare media result to media source */
    if (ret == 0) {
        ret = quicrq_compare_log_file(media_result_log, media_log_ref_path);
    }

    if (ret == 0) {
        ret = quicrq_compare_media_file(media_result_file, media_source_path);
    }

    /* CLear */
    if (qr_ctx != NULL) {
        quicrq_delete(qr_ctx);
    }

    return ret;
}

int quicrq_media_source_test()
{
    int ret = quicrq_media_publish_test_one(QUICRQ_TEST_VIDEO1_SOURCE, QUICRQ_TEST_VIDEO1_LOGREF, QUICRQ_TEST_VIDEO1_RESULT, QUICRQ_TEST_VIDEO1_LOG,
        &video_1mps, 0);

    return ret;
}

int quicrq_media_source_rt_test()
{
    int ret = quicrq_media_publish_test_one(QUICRQ_TEST_VIDEO1_SOURCE, QUICRQ_TEST_VIDEO1_RT_LOGREF, QUICRQ_TEST_VIDEO1_RT_RESULT, QUICRQ_TEST_VIDEO1_RT_LOG,
        &video_1mps, 1);

    return ret;
}

/* The media disorder test checks whether the test media consumer can properly manage 
 * the repair of holes. 
 */

typedef struct st_media_disorder_hole_t {
    uint64_t offset;
    size_t length;
    uint8_t media_buffer[1024];
} media_disorder_hole_t;

int quicrq_media_disorder_test_one(char const* media_source_name, char const* media_result_file, char const* media_result_log, size_t nb_losses, uint64_t* loss_pattern, size_t nb_dup)
{

    int ret = 0;
    char media_source_path[512];
    uint8_t media_buffer[1024];
    uint64_t current_time = 0;
    const uint64_t time_step = 1000;
    uint64_t published_offset = 0;
    size_t data_length;
    void* cons_ctx = NULL;
    FILE* F = NULL;
    media_disorder_hole_t* losses = NULL;
    size_t actual_losses = 0;
    size_t losses_size = sizeof(media_disorder_hole_t) * nb_losses;

    if (nb_losses == 0 || losses_size < sizeof(media_disorder_hole_t)) {
        ret = -1;
    }
    else {
        losses = (media_disorder_hole_t*)malloc(losses_size);
        if (losses == NULL) {
            ret = -1;
        }
    }

    if (ret == 0) {
        /* Locate the source and reference file */
        if (picoquic_get_input_path(media_source_path, sizeof(media_source_path),
            quicrq_test_solution_dir, media_source_name) != 0) {
            ret = -1;
        }
    }

    /* Initialize a consumer context for testing */
    if (ret == 0) {
        cons_ctx = test_media_consumer_init(media_result_file, media_result_log);
        if (cons_ctx == NULL) {
            ret = -1;
        }
    }

    /* Open the source file */
    if (ret == 0) {
        F = picoquic_file_open(media_source_path, "rb");
        if (F == NULL) {
            ret = -1;
        }
    }
   
    /* Loop through read and consume until finished, marking some frames as lost */
    while (ret == 0) {
        /* If the offset matches one of the expected errors, store the data for later correction */
        if (actual_losses < nb_losses && published_offset >= loss_pattern[actual_losses]) {
            losses[actual_losses].offset = published_offset;
            losses[actual_losses].length = fread(losses[actual_losses].media_buffer, 1, sizeof(losses[actual_losses].media_buffer), F);
            if (losses[actual_losses].length > 0) {
                published_offset += losses[actual_losses].length;
                actual_losses++;
            }
            else {
                /* reached end of file */
                break;
            }
        }
        else {
            /* Read a chunk of bytes in the buffer */
            data_length = fread(media_buffer, 1, sizeof(media_buffer), F);
            if (data_length > 0) {
                ret = test_media_consumer_cb(quicrq_media_datagram_ready, cons_ctx, current_time, media_buffer,
                    published_offset, data_length);
                if (ret != 0) {
                    DBG_PRINTF("Media consumer callback: ret = %d", ret);
                }
                published_offset += data_length;
            }
            else {
                /* reached end of file */
                break;
            }
        }
        current_time += time_step;
    }

    /* At this point, all blocks have been sent, except for the holes */
    if (ret == 0) {
        if (nb_dup > 0) {
            /* Fill some holes, in order to simulate duplication of repairs. */
            for (size_t n = 0; n < nb_dup; n++) {
                for (size_t i = 1; i < actual_losses; i += 2) {
                    /* Simulate repair of a hole */
                    ret = test_media_consumer_cb(quicrq_media_datagram_ready, cons_ctx, current_time, losses[i].media_buffer,
                        losses[i].offset, losses[i].length);
                    if (ret != 0) {
                        DBG_PRINTF("Media consumer callback: ret = %d", ret);
                    }
                }
            }
        }
        for (size_t i = 0; i < actual_losses; i++) {
            /* Simulate repair of a hole */
            ret = test_media_consumer_cb(quicrq_media_datagram_ready, cons_ctx, current_time, losses[i].media_buffer,
                losses[i].offset, losses[i].length);
            if (ret != 0) {
                DBG_PRINTF("Media consumer callback: ret = %d", ret);
            }
        }
    }

    /* Indicate the final offset, to simulate what datagrams would do */
    if (ret == 0) {
        ret = test_media_consumer_cb(quicrq_media_final_offset, cons_ctx, current_time, NULL, published_offset, 0);
        if (ret != 0) {
            DBG_PRINTF("Media consumer callback: ret = %d", ret);
        }
    }

    /* Close media file */
    if (F != NULL) {
        F = picoquic_file_close(F);
    }

    /* Close consumer */
    if (cons_ctx != NULL) {
        test_media_consumer_close(cons_ctx);
    }

    /* Compare media result to media source */
#if 0
    if (ret == 0) {
        ret = quicrq_compare_log_file(media_result_log, media_log_ref_path);
    }
#endif

    if (ret == 0) {
        ret = quicrq_compare_media_file(media_result_file, media_source_path);
    }

    return ret;
}

int quicrq_media_disorder_test()
{
    uint64_t loss_pattern[] = { 0, 4096, 8192, 9216, 20480 };

    int ret = quicrq_media_disorder_test_one(QUICRQ_TEST_VIDEO1_SOURCE, QUICRQ_TEST_VIDEO1_LOSS_RESULT, QUICRQ_TEST_VIDEO1_LOSS_LOG,
        sizeof(loss_pattern) / sizeof(uint64_t), loss_pattern, 3);
    return ret;
}