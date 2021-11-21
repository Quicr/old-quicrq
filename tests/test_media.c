
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include "quicrq.h"
#include "quicrq_reassembly.h"
#include "quicrq_internal.h"
#include "quicrq_tests.h"
#include "quicrq_test_internal.h"
#include "picoquic_utils.h"
#include "picosplay.h"
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

int test_media_frame_publisher_fn(
    quicrq_media_source_action_enum action,
    void* media_ctx,
    uint8_t* data,
    size_t data_max_size,
    size_t* data_length,
    int* is_last_segment,
    int* is_media_finished,
    uint64_t current_time)
{
    int ret = 0;
    test_media_publisher_context_t* pub_ctx = (test_media_publisher_context_t*)media_ctx;

    if (action == quicrq_media_source_get_data) {

        *is_media_finished = 0;
        *is_last_segment = 0;
        *data_length = 0;
        ret = test_media_publisher_check_frame(pub_ctx);

        if (ret == 0) {
            if (pub_ctx->is_finished) {
                *is_media_finished = 1;
            }
            else if (pub_ctx->media_frame_size > pub_ctx->media_frame_read) {
                if (!pub_ctx->is_real_time ||
                    current_time >= pub_ctx->current_header.timestamp) {
                    /* Copy data from frame in memory */
                    size_t available = pub_ctx->media_frame_size - pub_ctx->media_frame_read;
                    size_t copied = data_max_size;
                    if (data_max_size >= available) {
                        *is_last_segment = 1;
                        copied = available;
                    }
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
        ret = quicrq_publish_source(qr_ctx, url, url_length, srce_ctx, test_media_publisher_subscribe, test_media_frame_publisher_fn);
    }
    return ret;
}

/* Media receiver definitions.
 * Manage a list of frames being reassembled. The list is organized as a splay,
 * indexed by the frame id and frame offset. When a new segment is received
 * the code will check whether the frame is already present, and then whether the
 * segment for that frame has already arrived.
 */

typedef struct st_test_media_consumer_context_t {
    FILE* Res;
    FILE* Log;
    uint8_t header_bytes[QUIRRQ_MEDIA_TEST_HEADER_SIZE];
    quicrq_media_frame_header_t current_header;
    size_t media_frame_received;
    size_t target_size;

    quicrq_reassembly_context_t reassembly_ctx;

} test_media_consumer_context_t;

int test_media_derive_file_names(const uint8_t* url, size_t url_length, int is_datagram, int is_real_time, int is_post,
    char * result_file_name, char * result_log_name, size_t result_name_size)
{
    int ret = 0;
    int last_sep = 0;
    int last_dot = (int)url_length;
    int name_length = 0;
    for (size_t i = 0; i < url_length; i++) {
        if (url[i] == (uint8_t)'\\' || url[i] == (uint8_t)'/') {
            last_sep = (int)(i + 1);
            last_dot = (int)url_length;
        }
        else if (url[i] == (uint8_t)'.') {
            last_dot = (int)i;
        }
    }
    name_length = last_dot - last_sep;
    if (name_length <= 0 || name_length + 10 >= result_name_size) {
        ret = -1;
    }
    else {
        /* Derive file names from URL */
        memcpy(result_file_name, url + last_sep, name_length);
        result_file_name[name_length + 0] = '_';
        result_file_name[name_length + 1] = (is_post) ? 'P' : 'G';
        result_file_name[name_length + 2] = '_';
        result_file_name[name_length + 3] = (is_real_time) ? 'r' : 'n';
        result_file_name[name_length + 4] = '_';
        result_file_name[name_length + 5] = (is_datagram) ? 'd' : 's';
        result_file_name[name_length + 6] = '.';
        result_file_name[name_length + 7] = 'b';
        result_file_name[name_length + 8] = 'i';
        result_file_name[name_length + 9] = 'n';
        result_file_name[name_length + 10] = 0;
        memcpy(result_log_name, url + last_sep, name_length);
        result_log_name[name_length + 0] = '_';
        result_log_name[name_length + 1] = (is_post) ? 'P' : 'G';
        result_log_name[name_length + 2] = '_';
        result_log_name[name_length + 3] = (is_real_time) ? 'r' : 's';
        result_log_name[name_length + 4] = '_';
        result_log_name[name_length + 5] = (is_datagram) ? 'd' : 's';
        result_log_name[name_length + 6] = '.';
        result_log_name[name_length + 7] = 'c';
        result_log_name[name_length + 8] = 's';
        result_log_name[name_length + 9] = 'v';
        result_log_name[name_length + 10] = 0;
    }
    return ret;
}

int test_media_consumer_init_callback(quicrq_stream_ctx_t* stream_ctx, const uint8_t* url, size_t url_length)
{
    int ret = 0;
    void* media_ctx;
    char result_file_name[512];
    char result_log_name[512];

    ret = test_media_derive_file_names(url, url_length, stream_ctx->is_datagram, 1, 1, result_file_name, result_log_name, sizeof(result_file_name));

    if (ret == 0) {
        /* Init the local media consumer */
        media_ctx = test_media_consumer_init(result_file_name, result_log_name);

        if (media_ctx == NULL) {
            ret = -1;
        }
        else
        {
            /* set the parameter in the stream context. */
            ret = quicrq_set_media_stream_ctx(stream_ctx, test_media_frame_consumer_cb, media_ctx);
        }
    }

    return ret;
}

int test_media_consumer_close(void* media_ctx)
{
    /* Close result file and log file */
    int ret = 0;
    test_media_consumer_context_t* cons_ctx = (test_media_consumer_context_t*)media_ctx;

    if (cons_ctx->Res != NULL) {
        picoquic_file_close(cons_ctx->Res);
    }
    if (cons_ctx->Log != NULL) {
        picoquic_file_close(cons_ctx->Log);
    }

    quicrq_reassembly_release(&cons_ctx->reassembly_ctx);

    free(media_ctx);

    return ret;
}

void* test_media_consumer_init(char const* media_result_file, char const* media_result_log)
{
    /* Open and initialize result file and log file */
    test_media_consumer_context_t* cons_ctx = (test_media_consumer_context_t*)malloc(sizeof(test_media_consumer_context_t));
    if (cons_ctx != NULL) {
        int last_err;
        memset(cons_ctx, 0, sizeof(test_media_consumer_context_t));

        quicrq_reassembly_init(&cons_ctx->reassembly_ctx);

        if ((cons_ctx->Res = picoquic_file_open_ex(media_result_file, "wb", &last_err)) == NULL) {
            DBG_PRINTF("Cannot open %s, error: %d (0x%x)", media_result_file, last_err, last_err);
        }
        if ((cons_ctx->Log = picoquic_file_open_ex(media_result_log, "w", &last_err)) == NULL) {
            DBG_PRINTF("Cannot open %s, error: %d (0x%x)", media_result_log, last_err, last_err);
        }
        if (cons_ctx->Res == NULL || cons_ctx->Log == NULL) {
            (void)test_media_consumer_close(cons_ctx);
            cons_ctx = NULL;
        }
    }
    return cons_ctx;
}

int test_media_consumer_frame_ready(
    void* media_ctx,
    uint64_t current_time,
    uint64_t frame_id,
    const uint8_t* data,
    size_t data_length,
    quicrq_reassembly_frame_mode_enum frame_mode)
{
    int ret = 0;
    test_media_consumer_context_t* cons_ctx = (test_media_consumer_context_t*)media_ctx;

    /* Find the frame header */
    if (data_length < QUIRRQ_MEDIA_TEST_HEADER_SIZE) {
        /* Malformed frame */
        ret = -1;
    }
    else {
        quicrq_media_frame_header_t current_header;
        const uint8_t* fh = quicr_decode_frame_header(data,
            data + QUIRRQ_MEDIA_TEST_HEADER_SIZE, &current_header);
        if (fh == NULL) {
            ret = -1;
        }
        if (ret == 0) {
            /* if first time seen, document the delivery in the log */
            if (frame_mode != quicrq_reassembly_frame_repair) {
                if (fprintf(cons_ctx->Log, "%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%zu\n",
                    current_time, current_header.timestamp, current_header.number, current_header.length) <= 0) {
                    ret = -1;
                }
            }
        }
        if (ret == 0) {
            /* if in sequence, write the data to the file. */
            if (frame_mode != quicrq_reassembly_frame_peek) {
                if (fwrite(data, 1, data_length, cons_ctx->Res) != data_length) {
                    ret = -1;
                }
            }
        }
    }
    return ret;
}

int test_media_datagram_input(
    void* media_ctx,
    uint64_t current_time,
    const uint8_t* data,
    uint64_t frame_id,
    uint64_t offset,
    int is_last_segment,
    size_t data_length)
{
    test_media_consumer_context_t* cons_ctx = (test_media_consumer_context_t*)media_ctx;
    int ret = quicrq_reassembly_input(&cons_ctx->reassembly_ctx, current_time, data, frame_id, offset, is_last_segment, data_length, 
        test_media_consumer_frame_ready, media_ctx);

    return ret;
}

int test_media_consumer_learn_final_frame_id(
    void* media_ctx,
    uint64_t final_frame_id)
{
    int ret = 0;
    test_media_consumer_context_t* cons_ctx = (test_media_consumer_context_t*)media_ctx;

    ret = quicrq_reassembly_learn_final_frame_id(&cons_ctx->reassembly_ctx, final_frame_id);

    return ret;
}


int test_media_frame_consumer_cb(
    quicrq_media_consumer_enum action,
    void* media_ctx,
    uint64_t current_time,
    const uint8_t* data,
    uint64_t frame_id,
    uint64_t offset,
    int is_last_segment,
    size_t data_length)
{
    int ret = 0;
    test_media_consumer_context_t* cons_ctx = (test_media_consumer_context_t*)media_ctx;

    switch (action) {
    case quicrq_media_datagram_ready:
        ret = test_media_datagram_input(media_ctx, current_time, data, frame_id, (size_t)offset, is_last_segment, data_length);

        if (ret == 0 && cons_ctx->reassembly_ctx.is_finished) {
            ret = quicrq_consumer_finished;
        }
        break;
    case quicrq_media_final_frame_id:
        test_media_consumer_learn_final_frame_id(media_ctx, frame_id);

        if (ret == 0 && cons_ctx->reassembly_ctx.is_finished) {
            ret = quicrq_consumer_finished;
        }
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
        ret = quicrq_cnx_subscribe_media(cnx_ctx, url, url_length, use_datagrams, test_media_frame_consumer_cb, media_ctx);
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
    size_t data_length;
    void* srce_ctx = NULL;
    void* pub_ctx = NULL;
    void* cons_ctx = NULL;
    uint64_t frame_id = 0;
    uint64_t frame_offset = 0;
    int is_last_segment = 0;
    int is_media_finished = 0;
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
    while (ret == 0 && !is_media_finished && inactive < 32) {
        ret = test_media_frame_publisher_fn(quicrq_media_source_get_data,
            pub_ctx, media_buffer, sizeof(media_buffer),
            &data_length, &is_last_segment, &is_media_finished, current_time);
        if (ret != 0) {
            DBG_PRINTF("Publisher, ret=%d", ret);
        }
        else if (!is_media_finished && data_length == 0) {
            /* Update the current time to reflect media time */
            current_time = test_media_publisher_next_time(pub_ctx, current_time);
            inactive++;
        } else {
            inactive = 0;
            ret = test_media_frame_consumer_cb(quicrq_media_datagram_ready, cons_ctx, current_time, media_buffer,
                frame_id, frame_offset, is_last_segment, data_length);
            if (ret != 0) {
                DBG_PRINTF("Consumer, ret=%d", ret);
            }
            else if (is_last_segment) {
                frame_id++;
                frame_offset = 0;
            }
            else {
                frame_offset += data_length;
            }
        }
    }

    /* Close publisher and consumer */
    if (pub_ctx != NULL) {
        test_media_publisher_close(pub_ctx);
    }

    if (ret == 0) {
        ret = test_media_frame_consumer_cb(quicrq_media_final_frame_id, cons_ctx, current_time, NULL,
            frame_id, 0, 0, 0);
        if (ret == quicrq_consumer_finished) {
            ret = 0;
        }
        else {
            DBG_PRINTF("Consumer not finished after final offset! ret = %d", ret);
            ret = -1;
        }
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
#define QUICRQ_TEST_MEDIA_FRAME_RESULT "media_frame_result.bin"
#define QUICRQ_TEST_MEDIA_FRAME_LOG    "media_frame_log.csv"
#define QUICRQ_TEST_MEDIA_FRAME_LOSS_RESULT "media_frame_loss_result.bin"
#define QUICRQ_TEST_MEDIA_FRAME_LOSS_LOG    "media_frame_loss_log.csv"



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
    uint64_t frame_id = 0;
    uint64_t frame_offset = 0;
    int is_last_segment = 0;
    int is_media_finished = 0;
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
        if (ret == 0) {
            quicrq_wakeup_media_stream(stream_ctx);
        }
    }
    /* Initialize a consumer context for testing */
    if (ret == 0) {
        cons_ctx = test_media_consumer_init(media_result_file, media_result_log);
        if (cons_ctx == NULL) {
            ret = -1;
        }
    }

    /* Loop through publish and consume until finished */
    while (ret == 0 && !is_media_finished && inactive < 32) {
        ret = stream_ctx->publisher_fn(quicrq_media_source_get_data,
            stream_ctx->media_ctx, media_buffer, sizeof(media_buffer),
            &data_length, &is_last_segment, &is_media_finished, current_time);
        if (ret == 0) {
            if (is_media_finished || data_length > 0) {
                ret = test_media_frame_consumer_cb(quicrq_media_datagram_ready, cons_ctx, current_time, media_buffer,
                    frame_id, frame_offset, is_last_segment, data_length);
                if (ret == 0) {
                    published_offset += data_length;
                    inactive = 0;
                }
                else {
                    DBG_PRINTF("Frame consumer callback, ret: %d", ret);
                }
            }
            else {
                current_time = test_media_publisher_next_time(stream_ctx->media_ctx, current_time);
                inactive++;
            }
            if (is_last_segment) {
                frame_id++;
                frame_offset = 0;
            }
            else {
                frame_offset += data_length;
            }
        }
        else {
            DBG_PRINTF("Frame publisher callback, ret: %d", ret);
        }
    }

    /* Close publisher by closing the connection context */
    if (ret == 0) {
        ret = test_media_frame_consumer_cb(quicrq_media_final_frame_id, cons_ctx, current_time, NULL, frame_id, 0, 0, 0);
        if (ret == quicrq_consumer_finished) {
            ret = 0;
        }
        else {
            DBG_PRINTF("Consumer not finished after final frame id! ret = %d", ret);
            ret = -1;
        }
    }
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

/* Media datagram test. Check the datagram API.
 */

typedef struct st_media_disorder_hole_t {
    struct st_media_disorder_hole_t* next_loss;
    uint64_t frame_id;
    uint64_t offset;
    int is_last_segment;
    size_t length;
    uint8_t media_buffer[1024];
} media_disorder_hole_t;

int quicrq_media_datagram_test_one(char const* media_source_name, char const* media_result_file, char const* media_result_log, size_t nb_losses, uint64_t* loss_frame, int * loss_mode, size_t nb_dup)
{

    int ret = 0;
    char media_source_path[512];
    uint8_t media_buffer[1024];
    uint64_t current_time = 0;
    uint64_t next_srce_time = 0;
    size_t data_length;
    void* srce_ctx = NULL;
    void* pub_ctx = NULL;
    void* cons_ctx = NULL;
    size_t actual_losses = 0;
    int consumer_properly_finished = 0;
    uint64_t frame_id = 0;
    uint64_t frame_offset = 0;
    int is_media_finished = 0;
    int is_last_segment = 0;
    media_disorder_hole_t* first_loss = NULL;
    media_disorder_hole_t* last_loss = NULL;

    if (ret == 0) {
        /* Locate the source and reference file */
        if (picoquic_get_input_path(media_source_path, sizeof(media_source_path),
            quicrq_test_solution_dir, media_source_name) != 0) {
            ret = -1;
        }
    }

    /* Initialize a consumer context for testing */
    /* TODO: this should now be a frame consumer */
    if (ret == 0) {
        cons_ctx = test_media_consumer_init(media_result_file, media_result_log);
        if (cons_ctx == NULL) {
            ret = -1;
        }
    }

    /* Init the publisher context for testing */
    if (ret == 0) {
        srce_ctx = test_media_create_source(media_source_path, NULL, 1, &next_srce_time);
        if (srce_ctx != NULL) {
            if ((pub_ctx = test_media_publisher_subscribe((uint8_t*)media_source_path, strlen(media_source_path), srce_ctx)) == NULL) {
                ret = -1;
            }
        }
    }

    /* Loop through read and consume until finished, marking some frames as lost */
    while (ret == 0) {
        /* Get the next frame from the publisher */
        ret = test_media_frame_publisher_fn(
            quicrq_media_source_get_data, pub_ctx, media_buffer, sizeof(media_buffer),
            &data_length, &is_last_segment, &is_media_finished, current_time);
        if (ret != 0) {
            DBG_PRINTF("Media published function: ret = %d", ret);
            break;
        }
        if (data_length == 0) {
            if (is_media_finished) {
                break;
            }
            else {
                /* Update the simulated time and continue the loop */
                current_time = test_media_publisher_next_time(pub_ctx, current_time);
            }
        }
        /* Test whether to simulate losses or arrival */
        if (actual_losses < nb_losses &&
            (frame_id == loss_frame[actual_losses] || (loss_frame[actual_losses] == UINT64_MAX && is_media_finished)) &&
            (loss_mode[actual_losses] == 3 ||
                (loss_mode[actual_losses] == 0 && frame_offset == 0) ||
                (loss_mode[actual_losses] == 2 && is_last_segment == 0) ||
                (loss_mode[actual_losses == 1 && frame_offset != 0 && is_last_segment]))) {
            /* If the frame packet should be seen as lost, store it for repetition */
            media_disorder_hole_t* loss = (media_disorder_hole_t*)malloc(sizeof(media_disorder_hole_t));
            if (loss == NULL) {
                ret = -1;
                break;
            }
            memset(loss, 0, sizeof(media_disorder_hole_t));
            loss->frame_id = frame_id;
            loss->offset = frame_offset;
            loss->length = data_length;
            loss->is_last_segment = is_last_segment;
            memcpy(loss->media_buffer, media_buffer, data_length);
            if (last_loss == NULL) {
                first_loss = loss;
            }
            else {
                last_loss->next_loss = loss;
            }
            last_loss = loss;
        }
        else {
            /* Simulate arrival of packet */
            ret = test_media_frame_consumer_cb(quicrq_media_datagram_ready, cons_ctx, current_time, media_buffer,
                    frame_id, frame_offset, is_last_segment, data_length);
            if (ret != 0) {
                DBG_PRINTF("Media consumer callback: ret = %d", ret);
                break;
            }
        }
        /* Count the segments and the frames */
        if (is_last_segment) {
            frame_id++;
            frame_offset = 0;
            if (actual_losses < nb_losses &&
                (frame_id == loss_frame[actual_losses] || (loss_frame[actual_losses] == UINT64_MAX && is_media_finished))) {
                actual_losses++;
            }
        }
        else {
            frame_offset += data_length;
        }
    }

    /* Indicate the final frame_id, to simulate what datagrams would do */
    if (ret == 0) {
        ret = test_media_frame_consumer_cb(quicrq_media_final_frame_id, cons_ctx, current_time, NULL, frame_id, 0, 0, 0);
        if (ret == quicrq_consumer_finished) {
            consumer_properly_finished = 1;
            if (nb_losses > 0) {
                ret = -1;
            }
            else {
                ret = 0;
            }
        } 
        if (ret != 0) {
            DBG_PRINTF("Media consumer callback: ret = %d", ret);
        }
    }

    /* At this point, all blocks have been sent, except for the holes */
    /* TODO: holes should have frame numbers and offset */
    /* TODO: use the frame APIs */
    if (ret == 0 && first_loss != NULL) {
        media_disorder_hole_t* loss = first_loss;
        if (nb_dup > 0) {
            /* Fill some holes, in order to simulate duplication of repairs. */
            size_t actual_dup = 0;
            while (loss != NULL && actual_dup < nb_dup && ret == 0) {
                /* Simulate repair of a hole */
                actual_dup++;
                ret = test_media_frame_consumer_cb(quicrq_media_datagram_ready, cons_ctx, current_time, loss->media_buffer,
                    loss->frame_id, loss->offset, loss->is_last_segment, loss->length);
                if (ret != 0) {
                    DBG_PRINTF("Media consumer callback: ret = %d", ret);
                }
                else {
                    /* skip the next loss */
                    loss = loss->next_loss;
                    if (loss != NULL) {
                        loss = loss->next_loss;
                    }
                }
            }
        }
        /* Fill the remaining holes */
        loss = first_loss;
        while (loss != NULL && ret == 0) {
            /* Simulate repair of a hole */
            ret = test_media_frame_consumer_cb(quicrq_media_datagram_ready, cons_ctx, current_time, loss->media_buffer,
                loss->frame_id, loss->offset, loss->is_last_segment, loss->length);
            if (ret == quicrq_consumer_finished) {
                consumer_properly_finished = 1;
                ret = 0;
                break;
            }
            else if (ret != 0) {
                DBG_PRINTF("Media consumer callback: ret = %d", ret);
            }
            loss = loss->next_loss;
        }
    }

    if (ret == 0 && !consumer_properly_finished) {
        ret = -1;
        DBG_PRINTF("Consumer not properly finished, ret=%d", ret);
    }

    /* Close publisher */
    if (pub_ctx != NULL) {
        test_media_publisher_close(pub_ctx);
    }

    /* Close consumer */
    if (cons_ctx != NULL) {
        test_media_consumer_close(cons_ctx);
    }

    /* Free the memory allocated to losses.*/
    while (first_loss != NULL) {
        media_disorder_hole_t* loss = first_loss;
        first_loss = loss->next_loss;
        free(loss);
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

int quicrq_media_frame_noloss()
{
    int ret = quicrq_media_datagram_test_one(QUICRQ_TEST_VIDEO1_SOURCE, QUICRQ_TEST_MEDIA_FRAME_RESULT, QUICRQ_TEST_MEDIA_FRAME_LOG,
        0, NULL, NULL, 0);
    return ret;
}

int quicrq_media_frame_loss()
{
    uint64_t loss_frame[] = { 0, 4, 5, 6, 9, 11, 15, UINT64_MAX };
    int loss_mode[] = { 3, 3, 3, 3, 0,  1, 2,  3 };

    int ret = quicrq_media_datagram_test_one(QUICRQ_TEST_VIDEO1_SOURCE, QUICRQ_TEST_MEDIA_FRAME_LOSS_RESULT, QUICRQ_TEST_MEDIA_FRAME_LOSS_LOG,
        sizeof(loss_frame) / sizeof(uint64_t), loss_frame, loss_mode, 0);

    return ret;
}
