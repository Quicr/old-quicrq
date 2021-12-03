#ifndef QUICR_TEST_INTERNAL_H
#define QUICR_TEST_INTERNAL_H

#include <stdio.h>
#include "picoquic_utils.h"

#ifdef __cplusplus
extern "C" {
#endif
/* Test configuration: nodes, sources, addresses and links.
 * Each source is connected to a node, identified by a node_id
 * Nodes are connected via one-way links, identified by a link_id.
 * The links can be either symmetric or asymmetric. The link context
 * includes for each link the "return link" -- either itself or
 * a different link.
 * Each link can deliver data to a set of nodes. The relation between
 * the link and the nodes is an "attachment", which identifies the
 * link and a node, plus the IP address at which the node can receive data.
 * When a packet arrives on a node, the packet's IP address is used to
 * find which node shall receive it.
 * When a packet is posted on a link, the source IP address is posted
 * to the IP address of the attachment between that link and the return link.
 */

typedef struct st_quicrq_test_attach_t {
    int node_id;
    int link_id;
    struct sockaddr_storage node_addr;
} quicrq_test_attach_t;

typedef struct st_quicrq_test_source_t {
    uint64_t next_source_time;
    quicrq_media_source_ctx_t* srce_ctx;
} quicrq_test_source_t;

typedef struct st_quicrq_test_config_t {
    uint64_t simulated_time;
    uint64_t simulate_loss;
    char test_server_cert_file[512];
    char test_server_key_file[512];
    char test_server_cert_store_file[512];
    uint8_t ticket_encryption_key[16];
    int nb_nodes;
    quicrq_ctx_t** nodes;
    int nb_links;
    picoquictest_sim_link_t** links;
    int* return_links;
    int nb_attachments;
    quicrq_test_attach_t* attachments;
    int nb_sources;
    quicrq_test_source_t* sources;
    uint64_t cnx_error_client;
    uint64_t cnx_error_server;
} quicrq_test_config_t;

/* Create a test network configuration */
quicrq_test_config_t* quicrq_test_config_create(int nb_nodes, int nb_links, int nb_attachments, int nb_sources);
/* Delete a test network configuration */
void quicrq_test_config_delete(quicrq_test_config_t* config);
/* Find the address used by a test source to reach a destination */
struct sockaddr* quicrq_test_find_send_addr(quicrq_test_config_t* config, int srce_node_id, int dest_node_id);
/* Create a connection between two nodes */
quicrq_cnx_ctx_t* quicrq_test_create_client_cnx(quicrq_test_config_t* config, int client_node, int server_node);
/* Execute one round of the network simulation loop */
int quicrq_test_loop_step(quicrq_test_config_t* config, int* is_active);

/* Location of default media source file */
#ifdef _WINDOWS
#define QUICRQ_TEST_BASIC_SOURCE "tests\\video1_source.bin"
#else
#define QUICRQ_TEST_BASIC_SOURCE "tests/video1_source.bin"
#endif
extern char const* quicrq_test_picoquic_solution_dir;
extern char const* quicrq_test_solution_dir;

/* Test media definitions
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
int test_media_subscribe(quicrq_cnx_ctx_t* cnx_ctx, uint8_t* url, size_t url_length, int use_datagrams, char const* media_result_file, char const* media_result_log);
int quicrq_compare_media_file(char const* media_result_file, char const* media_reference_file);

int test_media_frame_consumer_cb(
    quicrq_media_consumer_enum action,
    void* media_ctx,
    uint64_t current_time,
    const uint8_t* data,
    uint64_t frame_id,
    uint64_t offset,
    int is_last_segment,
    size_t data_length);
void* test_media_publisher_init(char const* media_source_path, const generation_parameters_t* generation_model, int is_real_time);

void* test_media_consumer_init(char const* media_result_file, char const* media_result_log);
int test_media_consumer_init_callback(quicrq_stream_ctx_t* stream_ctx, const uint8_t* url, size_t url_length);
int test_media_derive_file_names(const uint8_t* url, size_t url_length, int is_datagram, int is_real_time, int is_post,
    char* result_file_name, char* result_log_name, size_t result_name_size);

#ifdef __cplusplus
}
#endif



#endif /* QUICR_TEST_INTERNAL_H */
