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
    generation_parameters_t* generation_context;
    unsigned int is_real_time : 1;
} test_media_source_context_t;

#ifdef __cplusplus
}
#endif



#endif /* QUICR_TEST_INTERNAL_H */
