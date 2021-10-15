#ifndef QUICR_TEST_INTERNAL_H
#define QUICR_TEST_INTERNAL_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

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
    uint64_t* p_next_time;
    uint8_t* media_frame;
    size_t media_frame_alloc;
    size_t media_frame_size;
    size_t media_frame_read;
    unsigned int is_real_time : 1;
    unsigned int is_finished : 1;
} test_media_publisher_context_t;

extern const generation_parameters_t video_1mps;

typedef struct st_test_media_source_context_t {
    char const* file_path;
    const generation_parameters_t* generation_context;
    unsigned int is_real_time : 1;
    uint64_t* p_next_time; /* Pointer for signalling next available time */
} test_media_source_context_t;

int test_media_publish(quicrq_ctx_t* qr_ctx, uint8_t* url, size_t url_length, char const* media_source_path, const generation_parameters_t* generation_model, int is_real_time, uint64_t* p_next_time);
int test_media_subscribe(quicrq_cnx_ctx_t* cnx_ctx, uint8_t* url, size_t url_length, char const* media_result_file, char const* media_result_log);
int quicrq_compare_media_file(char const* media_result_file, char const* media_reference_file);

#ifdef __cplusplus
}
#endif



#endif /* QUICR_TEST_INTERNAL_H */
