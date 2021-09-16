
#include <stdio.h>
#include <stdlib.h>
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

#define QUIRRQ_MEDIA_TEST_DEFAULT_SIZE 256
#define QUIRRQ_MEDIA_TEST_HEADER_SIZE 20

typedef struct st_generation_parameters_t {
    int target_duration;
    int frames_per_second;
    int nb_p_in_i;
    int frames_in_epoch;
    size_t target_p_min;
    size_t target_p_max;
    int nb_frames_elapsed;
    int nb_frames_sent;
} generation_parameters_t;

typedef struct st_test_media_publisher_context_t {
    FILE* F;
    generation_parameters_t* generation_context;
    quicrq_media_frame_header_t current_header;
    uint8_t* media_frame;
    size_t media_frame_alloc;
    size_t media_frame_size;
    size_t media_frame_read;
    unsigned int is_real_time : 1;
    unsigned int is_finished : 1;
} test_media_publisher_context_t;

const generation_parameters_t video_1mps = {
    10000000, 30, 10, 60, 4000, 5000, 0, 0};


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

void* test_media_publisher_init(char const* media_source_file, const generation_parameters_t * generation_model, int is_real_time)
{
    test_media_publisher_context_t* pub_ctx = (test_media_publisher_context_t*)
        malloc(sizeof(test_media_publisher_context_t));
    if (pub_ctx != NULL) {
        memset(pub_ctx, 0, sizeof(test_media_publisher_context_t));
        pub_ctx->is_real_time = (is_real_time != 0);
        pub_ctx->F = picoquic_file_open(media_source_file, "rb");

        if (pub_ctx->F == NULL) {
            if (generation_model != NULL) {
                pub_ctx->generation_context = (generation_parameters_t*)malloc(sizeof(generation_parameters_t));
                if (pub_ctx->generation_context != NULL) {
                    memcpy(pub_ctx->generation_context, generation_model, sizeof(generation_parameters_t));
                }
            }
            if (pub_ctx->generation_context == NULL) {
                free(pub_ctx);
                pub_ctx = NULL;
            }
        }
    }
    return pub_ctx;
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
            if (pub_ctx->media_frame_size > 0) {
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
            /* Bug. Assume this is the end of file. */
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
                    }
                    else {
                        pub_ctx->media_frame_size = target_size;
                    }
                }
            }
            else {
                /* malformed header ! */
                ret = -1;
            }       
        }
        return ret;
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

int test_media_publisher_cb(
    void* media_ctx,
    uint8_t* data,
    size_t data_max_size,
    size_t* data_length,
    int * is_finished,
    uint64_t current_time)
{
    int ret = 0;
    test_media_publisher_context_t* pub_ctx = (test_media_publisher_context_t*)media_ctx;

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
    /* TODO: matters of time */
    if (ret == 0) {
        if (pub_ctx->is_finished) {
            *is_finished = 1;
            *data_length = 0;
        }
        else if (pub_ctx->media_frame_size > pub_ctx->media_frame_read) {
            /* Copy data from frame in memory */
            size_t available = pub_ctx->media_frame_size - pub_ctx->media_frame_read;
            size_t copied = (available > data_max_size) ? data_max_size : available;
            memcpy(data, pub_ctx->media_frame + pub_ctx->media_frame_read, copied);
            *data_length = copied;
            pub_ctx->media_frame_read += copied;
        }
    }
    return ret;
}

/* Media receiver definitions */
typedef struct st_test_media_consumer_context_t {
    FILE* Res;
    FILE* Log;
    uint8_t header_bytes[QUIRRQ_MEDIA_TEST_HEADER_SIZE];
    quicrq_media_frame_header_t current_header;
    size_t media_frame_received;
    size_t target_size;
    unsigned int is_finished : 1;
    unsigned int header_received : 1;
} test_media_consumer_context_t;

void test_media_consumer_close(void* media_ctx)
{
    /* Close result file and log file */
    test_media_consumer_context_t* cons_ctx = (test_media_consumer_context_t*)media_ctx;
    if (cons_ctx->Res != NULL) {
        picoquic_file_close(cons_ctx->Res);
    }
    if (cons_ctx->Log != NULL) {
        picoquic_file_close(cons_ctx->Log);
    }
    free(media_ctx);
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
            test_media_consumer_close(cons_ctx);
            cons_ctx = NULL;
        }
    }
    return cons_ctx;
}

int test_media_consumer_cb(
    void* media_ctx,
    uint64_t current_time,
    uint8_t* data, 
    size_t data_length,
    int is_finished)
{
    int ret = 0;
    size_t processed = 0;
    test_media_consumer_context_t* cons_ctx = (test_media_consumer_context_t*)media_ctx;
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
    if (is_finished) {
        cons_ctx->is_finished = 1;
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
    }
    else {
        /* Read the frames on both. They should match, or both should come to an end */
        while (ret == 0 && !result_ctx->is_finished && !ref_ctx->is_finished) {
            ret = test_media_read_frame_from_file(result_ctx);
            if (ret == 0) {
                ret = test_media_read_frame_from_file(ref_ctx);
                if (ret == 0) {
                    /* Compare the media frames */
                    if (result_ctx->is_finished) {
                        if (!ref_ctx->is_finished) {
                            ret = -1;
                        }
                    }
                    else if (ref_ctx->is_finished) {
                        if (!result_ctx->is_finished) {
                            ret = -1;
                        }
                    }
                    else if (ref_ctx->current_header.timestamp != result_ctx->current_header.timestamp) {
                        ret = -1;
                    }
                    else if (ref_ctx->current_header.number != result_ctx->current_header.number) {
                        ret = -1;
                    }
                    else if (ref_ctx->current_header.length != result_ctx->current_header.length) {
                        ret = -1;
                    }
                    else if (ref_ctx->media_frame_size != result_ctx->media_frame_size){
                        ret = -1;
                    }
                    else if (memcmp(ref_ctx->media_frame, result_ctx->media_frame, ref_ctx->media_frame_size) != 0) {
                        ret = -1;
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
    size_t data_length;
    void* pub_ctx = NULL;
    void* cons_ctx = NULL;
    int is_finished = 0;

    /* Locate the source and reference file */
    if (picoquic_get_input_path(media_source_path, sizeof(media_source_path),
        quicrq_test_solution_dir, media_source_name) != 0 ||
        picoquic_get_input_path(media_log_ref_path, sizeof(media_log_ref_path),
            quicrq_test_solution_dir, media_log_reference) != 0){
        ret = -1;
    }

    /* Init the publisher and consumer */
    if (ret == 0) {
        pub_ctx = test_media_publisher_init(media_source_path, generation_model, is_real_time);
        cons_ctx = test_media_consumer_init(media_result_file, media_result_log);
        if (pub_ctx == NULL || cons_ctx == NULL){
            ret = -1;
        }
    }

    /* Loop through publish and consume until finished */
    while (ret == 0 && !is_finished) {
        ret = test_media_publisher_cb(pub_ctx, media_buffer, sizeof(media_buffer),
            &data_length, &is_finished, current_time);
        if (ret == 0) {
            ret = test_media_consumer_cb(cons_ctx, current_time, media_buffer,
                data_length, is_finished);
        }
    }

    /* Close publisher and consumer */
    if (pub_ctx != NULL) {
        test_media_publisher_close(pub_ctx);
    }

    /* Compare media result to media source */
    if (cons_ctx != NULL) {
        test_media_consumer_close(cons_ctx);
    }

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
#else
#define QUICRQ_TEST_VIDEO1_SOURCE "tests/video1_source.bin"
#define QUICRQ_TEST_VIDEO1_LOGREF "tests/video1_logref.csv"
#endif
#define QUICRQ_TEST_VIDEO1_RESULT "video1_result.bin"
#define QUICRQ_TEST_VIDEO1_LOG    "video1_log.csv"

int quicrq_media_video1_test()
{
    int ret = quicrq_media_api_test_one(QUICRQ_TEST_VIDEO1_SOURCE, QUICRQ_TEST_VIDEO1_LOGREF, QUICRQ_TEST_VIDEO1_RESULT, QUICRQ_TEST_VIDEO1_LOG,
        &video_1mps, 0);

    return ret;
}