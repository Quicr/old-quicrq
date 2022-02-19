#include <stdlib.h>
#include <string.h>
#include <picoquic.h>
#include <picoquic_utils.h>
#include <picoquic_set_textlog.h>
#include <picoquic_set_binlog.h>

#include "quicrq.h"
#include "quicrq_internal.h"
#include "quicrq_tests.h"
#include "quicrq_test_internal.h"
#ifdef _WINDOWS
#ifdef _WINDOWS64
#define QUICRQ_PICOQUIC_DEFAULT_SOLUTION_DIR "..\\..\\..\\picoquic\\"
#define QUICRQ_DEFAULT_SOLUTION_DIR "..\\..\\"
#else
#define QUICRQ_PICOQUIC_DEFAULT_SOLUTION_DIR "..\\..\\picoquic\\"
#define QUICRQ_DEFAULT_SOLUTION_DIR "..\\"
#endif
#else
#define QUICRQ_PICOQUIC_DEFAULT_SOLUTION_DIR "../picoquic/"
#define QUICRQ_DEFAULT_SOLUTION_DIR "./"
#endif

char const* quicrq_test_picoquic_solution_dir = QUICRQ_PICOQUIC_DEFAULT_SOLUTION_DIR;
char const* quicrq_test_solution_dir = QUICRQ_DEFAULT_SOLUTION_DIR;



/* Find arrival context by link ID and destination address */
int quicrq_test_find_dest_node(quicrq_test_config_t* config, int link_id, struct sockaddr* addr)
{
    int node_id = -1;

    for (int d_attach = 0; d_attach < config->nb_attachments; d_attach++) {
        if (config->attachments[d_attach].link_id == link_id &&
            picoquic_compare_addr((struct sockaddr*)&config->attachments[d_attach].node_addr, addr) == 0) {
            node_id = config->attachments[d_attach].node_id;
            break;
        }
    }
    return (node_id);
}

/* Find departure link by destination address.
 * The code verifies that the return link is present.
 * If srce_addr is prsent and set to AF_UNSPEC, it is filled with appropriate address.
 */
int quicrq_test_find_send_link(quicrq_test_config_t* config, int srce_node_id, const struct sockaddr* dest_addr, struct sockaddr_storage* srce_addr)
{
    int dest_link_id = -1;

    for (int s_attach = 0; s_attach < config->nb_attachments && dest_link_id == -1; s_attach++) {
        if (config->attachments[s_attach].node_id == srce_node_id) {
            int link_id = config->return_links[config->attachments[s_attach].link_id];
            for (int d_attach = 0; d_attach < config->nb_attachments; d_attach++) {
                if (config->attachments[d_attach].link_id == link_id &&
                    picoquic_compare_addr((struct sockaddr*)&config->attachments[d_attach].node_addr, dest_addr) == 0) {
                    if (srce_addr != NULL && srce_addr->ss_family == AF_UNSPEC) {
                        picoquic_store_addr(srce_addr, (struct sockaddr*)&config->attachments[s_attach].node_addr);
                    }
                    dest_link_id = config->attachments[d_attach].link_id;
                    break;
                }
            }
        }
    }

    return dest_link_id;
}

/* Find destination address from source and destination node id. */
struct sockaddr* quicrq_test_find_send_addr(quicrq_test_config_t* config, int srce_node_id, int dest_node_id)
{
    struct sockaddr* dest_addr = NULL;
    for (int s_attach = 0; s_attach < config->nb_attachments && dest_addr == NULL; s_attach++) {
        if (config->attachments[s_attach].node_id == srce_node_id) {
            int link_id = config->return_links[config->attachments[s_attach].link_id];
            for (int d_attach = 0; d_attach < config->nb_attachments; d_attach++) {
                if (config->attachments[d_attach].link_id == link_id &&
                    config->attachments[d_attach].node_id == dest_node_id){
                    dest_addr = (struct sockaddr*)&config->attachments[d_attach].node_addr;
                    break;
                }
            }
        }
    }

    return dest_addr;
}

/* Packet departure from selected node */
int quicrq_test_packet_departure(quicrq_test_config_t* config, int node_id, int* is_active)
{
    int ret = 0;
    picoquictest_sim_packet_t* packet = picoquictest_sim_link_create_packet();

    if (packet == NULL) {
        /* memory error during test. Something is really wrong. */
        ret = -1;
    }
    else {
        /* check whether there is something to send */
        int if_index = 0;

        ret = picoquic_prepare_next_packet(config->nodes[node_id]->quic, config->simulated_time,
            packet->bytes, PICOQUIC_MAX_PACKET_SIZE, &packet->length,
            &packet->addr_to, &packet->addr_from, &if_index, NULL, NULL);

        if (ret != 0)
        {
            /* useless test, but makes it easier to add a breakpoint under debugger */
            free(packet);
            ret = -1;
        }
        else if (packet->length > 0) {
            /* Find the exit link. This assumes destination addresses are available on only one link */
            int link_id = quicrq_test_find_send_link(config, node_id, (struct sockaddr*)&packet->addr_to, &packet->addr_from);

            if (link_id >= 0) {
                *is_active = 1;
                picoquictest_sim_link_submit(config->links[link_id], packet, config->simulated_time);
            }
            else {
                /* packet cannot be routed. */
                free(packet);
            }
        }
    }

    return ret;
}

/* Process arrival of a packet from a link */
int quicrq_test_packet_arrival(quicrq_test_config_t* config, int link_id, int * is_active)
{
    int ret = 0;
    picoquictest_sim_packet_t* packet = picoquictest_sim_link_dequeue(config->links[link_id], config->simulated_time);

    if (packet == NULL) {
        /* unexpected, probably bug in test program */
        ret = -1;
    }
    else {
        int node_id = quicrq_test_find_dest_node(config, link_id, (struct sockaddr*)&packet->addr_to);
        uint64_t loss = (config->simulate_loss & 1);
        config->simulate_loss >>= 1;
        config->simulate_loss |= (loss << 63);

        if (node_id >= 0 && loss == 0) {
            *is_active = 1;

            ret = picoquic_incoming_packet(config->nodes[node_id]->quic,
                packet->bytes, (uint32_t)packet->length,
                (struct sockaddr*)&packet->addr_from,
                (struct sockaddr*)&packet->addr_to, 0, 0,
                config->simulated_time);
        }
        else {
            /* simulated loss */
        }
        free(packet);
    }

    return ret;
}

/* Execute the loop */
int quicrq_test_loop_step_ex(quicrq_test_config_t* config, int* is_active, uint64_t app_wake_time)
{
    int ret = 0;
    int next_step_type = 0;
    int next_step_index = 0;
    uint64_t next_time = UINT64_MAX;

    /* Check which source has the lowest time */
    for (int i = 0; i < config->nb_sources; i++) {
        if (config->sources[i].next_source_time < next_time) {
            next_time = config->sources[i].next_source_time;
            next_step_type = 1;
            next_step_index = i;
        }
    }

    /* Check which node has the lowest wait time */
    for (int i = 0; i < config->nb_nodes; i++) {
        uint64_t quic_time = picoquic_get_next_wake_time(config->nodes[i]->quic, config->simulated_time);
        if (quic_time < next_time) {
            next_time = quic_time;
            next_step_type = 2;
            next_step_index = i;
        }
    }
    /* Check which link has the lowest arrival time */
    for (int i = 0; i < config->nb_links; i++) {
        if (config->links[i]->first_packet != NULL &&
            config->links[i]->first_packet->arrival_time < next_time) {
            next_time = config->links[i]->first_packet->arrival_time;
            next_step_type = 3;
            next_step_index = i;
        }
    }

    if (next_time > app_wake_time) {
        /* Special case. pretend that node 0 has to be waken up */
        next_time = app_wake_time;
        next_step_type = 2;
        next_step_index = 0;
    }

    if (next_time < UINT64_MAX) {
        /* Update the time */
        if (next_time > config->simulated_time) {
            config->simulated_time = next_time;
        }
        switch (next_step_type) {
        case 1: /* Media ready on source #next_step_index */
            quicrq_source_wakeup(config->sources[next_step_index].srce_ctx);
            config->sources[next_step_index].next_source_time = UINT64_MAX;
            break;
        case 2: /* Quicrq context #next_step_index is ready to send data */
            ret = quicrq_test_packet_departure(config, next_step_index, is_active);
            break;
        case 3:
            /* If arrival, take next packet, find destination by address, and submit to end-of-link context */
            ret = quicrq_test_packet_arrival(config, next_step_index, is_active);
            break;
        default:
            /* This should never happen! */
            ret = -1;
            break;
        }
    }
    else {
        ret = -1;
    }

    return ret;
}

/* Execute the loop */
int quicrq_test_loop_step(quicrq_test_config_t* config, int* is_active) {
    return quicrq_test_loop_step_ex(config, is_active, UINT64_MAX);
}

/* Manage different file names for different receivers and sources */
void quicrq_test_config_target_free(quicrq_test_config_target_t* target)
{
    if (target->target_bin != NULL) {
        free(target->target_bin);
    }

    if (target->target_csv != NULL) {
        free(target->target_csv);
    }

    free(target);
}

quicrq_test_config_target_t* quicrq_test_config_target_create(char const* test_id, char const* url, int client_id, char const* ref)
{
    quicrq_test_config_target_t* target = (quicrq_test_config_target_t*)malloc(sizeof(quicrq_test_config_target_t));

    if (target != NULL) {
        size_t id_len = strlen(test_id);
        size_t url_length = strlen(url);
        size_t name_len = id_len + 1 + url_length + 1 + 9 + 1 + 3 + 1;

        memset(target, 0, sizeof(quicrq_test_config_target_t));
        target->url = url;
        target->ref = ref;
        target->url_length = url_length;
        target->target_bin = (char*)malloc(name_len);
        target->target_csv = (char*)malloc(name_len);
        if (target->target_bin != NULL && target->target_csv != NULL) {
            (void)picoquic_sprintf(target->target_bin, name_len, NULL, "%s_%s_%d.bin", test_id, url, client_id);
            (void)picoquic_sprintf(target->target_csv, name_len, NULL, "%s_%s_%d.csv", test_id, url, client_id);
        }
        else {
            quicrq_test_config_target_free(target);
            target = NULL;
        }
    }
    return target;
}

/* Delete a configuration */
void quicrq_test_config_delete(quicrq_test_config_t* config)
{
    if (config->nodes != NULL) {
        for (int i = 0; i < config->nb_nodes; i++) {
            if (config->nodes[i] != NULL) {
                quicrq_delete(config->nodes[i]);
            }
        }
        free(config->nodes);
    }

    if (config->links != NULL) {
        for (int i = 0; i < config->nb_links; i++) {
            if (config->links[i] != NULL) {
                picoquictest_sim_link_delete(config->links[i]);
            }
        }
        free(config->links);
    }

    if (config->return_links != NULL) {
        free(config->return_links);
    }

    if (config->attachments != NULL) {
        free(config->attachments);
    }

    if (config->sources != NULL) {
        free(config->sources);
    }

    free(config);
}

/* Create a configuration */
quicrq_test_config_t* quicrq_test_config_create(int nb_nodes, int nb_links, int nb_attachments, int nb_sources)
{
    quicrq_test_config_t* config = (quicrq_test_config_t*)malloc(sizeof(quicrq_test_config_t));

    if (config != NULL) {
        int success = 1;

        memset(config, 0, sizeof(quicrq_test_config_t));
        memset(config->ticket_encryption_key, 0x55, sizeof(config->ticket_encryption_key));

        /* Locate the default cert, key and root in the Picoquic solution*/
        if (picoquic_get_input_path(config->test_server_cert_file, sizeof(config->test_server_cert_file),
            quicrq_test_picoquic_solution_dir, PICOQUIC_TEST_FILE_SERVER_CERT) != 0 ||
            picoquic_get_input_path(config->test_server_key_file, sizeof(config->test_server_key_file),
                quicrq_test_picoquic_solution_dir, PICOQUIC_TEST_FILE_SERVER_KEY) != 0 ||
            picoquic_get_input_path(config->test_server_cert_store_file, sizeof(config->test_server_cert_store_file),
                quicrq_test_picoquic_solution_dir, PICOQUIC_TEST_FILE_CERT_STORE) != 0){
            success = 0;
        }

        if (nb_nodes <= 0 || nb_nodes > 0xffff) {
            success = 0;
        }
        else if (success) {
            config->nodes = (quicrq_ctx_t**)malloc(nb_nodes * sizeof(quicrq_ctx_t*));
            success &= (config->nodes != NULL);
            if (success) {
                memset(config->nodes, 0, nb_nodes * sizeof(quicrq_ctx_t*));
                config->nb_nodes = nb_nodes;
            }
        }

        if (nb_links <= 0 || nb_links > 0xffff) {
            success = 0;
        } else if (success) {
            config->links = (picoquictest_sim_link_t**)malloc(nb_links * sizeof(picoquictest_sim_link_t*));
            config->return_links = (int*)malloc(nb_links * sizeof(int));
            success &= (config->links != NULL);

            if (success) {
                memset(config->links, 0, nb_links * sizeof(picoquictest_sim_link_t*));
                config->nb_links = nb_links;

                for (int i = 0; success && (i < nb_links); i++) {
                    picoquictest_sim_link_t* link = picoquictest_sim_link_create(0.01, 10000, NULL, 0, config->simulated_time);
                    config->links[i] = link;
                    success &= (link != NULL);
                }
            }
        }

        if (nb_attachments <= 0 || nb_attachments > 0xffff) {
            success = 0;
        } else if (success) {
            config->attachments = (quicrq_test_attach_t*)malloc(nb_attachments * sizeof(quicrq_test_attach_t));
            success &= (config->attachments != NULL);

            if (success) {
                memset(config->attachments, 0, nb_attachments * sizeof(quicrq_test_attach_t));
                config->nb_attachments = nb_attachments;
                for (int i = 0; success && (i < config->nb_attachments); i++) {
                    char addr_text[128];
                    quicrq_test_attach_t* p_attach = &config->attachments[i];

                    if (picoquic_sprintf(addr_text, sizeof(addr_text), NULL, "%x::%x", i + 0x1000, i + 0x1000) == 0) {
                        picoquic_store_text_addr(&p_attach->node_addr, addr_text, i + 0x1000);
                    }
                    else {
                        success = 0;
                    }
                }
            }
        }

        if (nb_sources <= 0 || nb_sources > 0xffff) {
            success = 0;
        }
        else if (success) {
            config->sources = (quicrq_test_source_t*)malloc(nb_sources * sizeof(quicrq_test_source_t));
            success &= (config->sources != NULL);

            if (success) {
                memset(config->sources, 0, nb_sources * sizeof(quicrq_test_source_t));
                config->nb_sources = nb_sources;
            }
        }

        if (!success) {
            quicrq_test_config_delete(config);
            config = NULL;
        }
    }

    return config;
}

quicrq_test_config_t* quicrq_test_basic_config_create(uint64_t simulate_loss)
{
    /* Create a configuration with just two nodes, two links, one source and two attachment points.*/
    quicrq_test_config_t* config = quicrq_test_config_create(2, 2, 2, 1);
    if (config != NULL) {
        /* Create the contexts for the origin and the client */
        config->nodes[0] = quicrq_create(QUICRQ_ALPN,
            config->test_server_cert_file, config->test_server_key_file, NULL, NULL, NULL,
            config->ticket_encryption_key, sizeof(config->ticket_encryption_key),
            &config->simulated_time);
        config->nodes[1] = quicrq_create(QUICRQ_ALPN,
            NULL, NULL, config->test_server_cert_store_file, NULL, NULL,
            NULL, 0, &config->simulated_time);
        config->sources[0].srce_ctx = NULL;
        if (config->nodes[0] == NULL || config->nodes[1] == NULL) {
            quicrq_test_config_delete(config);
            config = NULL;
        }
    }
    if (config != NULL) {
        /* Populate the attachments */
        config->return_links[0] = 1;
        config->attachments[0].link_id = 0;
        config->attachments[0].node_id = 0;
        config->return_links[1] = 0;
        config->attachments[1].link_id = 1;
        config->attachments[1].node_id = 1;
        /* Set the desired loss pattern */
        config->simulate_loss = simulate_loss;
    }
    return config;
}

quicrq_cnx_ctx_t* quicrq_test_create_client_cnx(quicrq_test_config_t* config, int client_node, int server_node)
{
    quicrq_ctx_t* qr_ctx = config->nodes[client_node];
    quicrq_cnx_ctx_t* cnx_ctx = NULL;
    struct sockaddr* addr_to = NULL;
    
    /* Find an attachment leading to server node */
    for (int i = 0; i < config->nb_attachments; i++) {
        addr_to = quicrq_test_find_send_addr(config, client_node, server_node);
    }

    if (addr_to != NULL) {
        cnx_ctx = quicrq_create_client_cnx(qr_ctx, NULL, addr_to);
    }
    return cnx_ctx;
}

/* Basic connection test */
int quicrq_basic_test_one(int is_real_time, int use_datagrams, uint64_t simulate_losses, int is_from_client)
{
    int ret = 0;
    int nb_steps = 0;
    int nb_inactive = 0;
    int is_closed = 0;
    const uint64_t max_time = 360000000;
    const int max_inactive = 128;
    quicrq_test_config_t* config = quicrq_test_basic_config_create(simulate_losses);
    quicrq_cnx_ctx_t* cnx_ctx = NULL;
    char media_source_path[512];
    char result_file_name[512];
    char result_log_name[512];
    char text_log_name[512];
    size_t nb_log_chars = 0;

    (void)picoquic_sprintf(text_log_name, sizeof(text_log_name), &nb_log_chars, "basic_textlog-%d-%d-%d-%llx.txt", is_real_time, use_datagrams, is_from_client, (unsigned long long)simulate_losses);
    ret = test_media_derive_file_names((uint8_t*)QUICRQ_TEST_BASIC_SOURCE, strlen(QUICRQ_TEST_BASIC_SOURCE),
        use_datagrams, is_real_time, is_from_client,
        result_file_name, result_log_name, sizeof(result_file_name));

    if (config == NULL) {
        ret = -1;
    }

    /* Locate the source and reference file */
    if (picoquic_get_input_path(media_source_path, sizeof(media_source_path),
        quicrq_test_solution_dir, QUICRQ_TEST_BASIC_SOURCE) != 0) {
        ret = -1;
    }

    /* Add QUIC level log */
    if (ret == 0) {
        ret = picoquic_set_textlog(config->nodes[1]->quic, text_log_name);
    }

    if (ret == 0) {
        /* Add a test source to the configuration, and to the either the client or the server */
        int publish_node = (is_from_client) ? 1 : 0;

        config->sources[0].srce_ctx = test_media_publish(config->nodes[publish_node], (uint8_t*)QUICRQ_TEST_BASIC_SOURCE,
            strlen(QUICRQ_TEST_BASIC_SOURCE), media_source_path, NULL, is_real_time,
            &config->sources[0].next_source_time, 0);
        if (config->sources[0].srce_ctx == NULL) {
            ret = -1;
            DBG_PRINTF("Cannot publish test media %s, ret = %d", QUICRQ_TEST_BASIC_SOURCE, ret);
        }
    }

    if (ret == 0) {
        /* Create a quirq connection context on client */
        cnx_ctx = quicrq_test_create_client_cnx(config, 1, 0);
        if (cnx_ctx == NULL) {
            ret = -1;
            DBG_PRINTF("Cannot create client connection, ret = %d", ret);
        }
    }

    if (ret == 0) {
        if (is_from_client) {
            /* Set up a default receiver on the server */
            quicrq_set_media_init_callback(config->nodes[0], test_media_consumer_init_callback);
            /* Start pushing from the client */
            ret = quicrq_cnx_post_media(cnx_ctx, (uint8_t*)QUICRQ_TEST_BASIC_SOURCE, strlen(QUICRQ_TEST_BASIC_SOURCE), use_datagrams);
        }
        else {
            /* Create a subscription to the test source on client */
            ret = test_media_subscribe(cnx_ctx, (uint8_t*)QUICRQ_TEST_BASIC_SOURCE, strlen(QUICRQ_TEST_BASIC_SOURCE), use_datagrams, result_file_name, result_log_name);
            if (ret != 0) {
                DBG_PRINTF("Cannot subscribe to test media %s, ret = %d", QUICRQ_TEST_BASIC_SOURCE, ret);
            }
        }
    }

    while (ret == 0 && nb_inactive < max_inactive && config->simulated_time < max_time) {
        /* Run the simulation. Monitor the connection. Monitor the media. */
        int is_active = 0;

        ret = quicrq_test_loop_step(config, &is_active);
        if (ret != 0) {
            DBG_PRINTF("Fail on loop step %d, %d, active: ret=%d", nb_steps, is_active, ret);
        }

        nb_steps++;

        if (is_active) {
            nb_inactive = 0;
        }
        else {
            nb_inactive++;
            if (nb_inactive >= max_inactive) {
                DBG_PRINTF("Exit loop after too many inactive: %d", nb_inactive);
            }
        }
        /* if the media is received, exit the loop */
        if (config->nodes[1]->first_cnx == NULL) {
            DBG_PRINTF("%s", "Exit loop after client connection closed.");
            break;
        } else {
            int client_stream_closed = config->nodes[1]->first_cnx->first_stream == NULL;
            int server_stream_closed = config->nodes[0]->first_cnx != NULL && config->nodes[0]->first_cnx->first_stream == NULL;

            if (!is_closed && client_stream_closed && server_stream_closed){
                /* Client is done. Close connection without waiting for timer */
                ret = picoquic_close(config->nodes[1]->first_cnx->cnx, 0);
                is_closed = 1;
                if (ret != 0) {
                    DBG_PRINTF("Cannot close client connection, ret = %d", ret);
                }
            }
        }
    }

    if (ret == 0 && (!is_closed || config->simulated_time > 12000000)) {
        DBG_PRINTF("Session was not properly closed, time = %" PRIu64, config->simulated_time);
        ret = -1;
    }

    /* Clear everything. */
    if (config != NULL) {
        quicrq_test_config_delete(config);
    }
    /* Verify that media file was received correctly */
    if (ret == 0) {
        ret = quicrq_compare_media_file(result_file_name, media_source_path);
    }
    else {
        DBG_PRINTF("Test failed before getting results, ret = %d", ret);
    }

    return ret;
}

/* Basic connection test, using streams, not real time. */
int quicrq_basic_test()
{
    return quicrq_basic_test_one(0, 0, 0, 0);
}

/* Basic connection test, using streams, real time. */
int quicrq_basic_rt_test()
{
    return quicrq_basic_test_one(1, 0, 0, 0);
}

/* Basic datagram test. Same as the basic test, but using datagrams instead of streams. */
int quicrq_datagram_basic_test()
{
    return quicrq_basic_test_one(1, 1, 0, 0);
}

/* Datagram test, with forced packet losses. */
int quicrq_datagram_loss_test()
{
    return quicrq_basic_test_one(1, 1, 0x7080, 0);
}

/* Publish from client, using streams */
int quicrq_basic_client_test()
{
    return quicrq_basic_test_one(1, 0, 0, 1);
}

/* Publish from client, using datagrams */
int quicrq_datagram_client_test()
{
    return quicrq_basic_test_one(1, 1, 0, 1);
}

/* Unit tests of reordering functions.
 * Check that the frames are being sent as expected.
 * Set up: provide a list of frame ids in the receive buffer.
 * Verify that the successive transmitted ID correspond to the buffer content.
 * Add a series of additional frames.
 * Verify that they are all sent.
 * repeat.
 */

int quicrq_relay_add_frame_to_cache(struct st_quicrq_relay_cached_media_t* cached_ctx,
    uint64_t frame_id,
    const uint8_t* data,
    size_t data_length);
int quicrq_relay_next_available_frame(quicrq_sent_frame_ranges_t* frame_ranges, struct st_quicrq_relay_cached_media_t* cached_media, uint64_t* next_frame_id);
int quicrq_relay_add_frame_id_to_ranges(quicrq_sent_frame_ranges_t* frame_ranges, uint64_t frame_id);
struct st_quicrq_relay_cached_media_t* quicrq_relay_create_cache_ctx();
void quicrq_relay_delete_cache_ctx(struct st_quicrq_relay_cached_media_t* cache_ctx);
void quick_relay_clear_ranges(quicrq_sent_frame_ranges_t* frame_ranges);

int quick_relay_range_test_wave(quicrq_sent_frame_ranges_t* frame_ranges, struct st_quicrq_relay_cached_media_t* cached_media, uint64_t* wave, size_t nb_in_wave)
{
    int ret = 0;
    uint8_t data[] = { 'w', 'h', 'a', 'e', 'v', 'e', 'r' };
    size_t data_length = sizeof(data);
    /* Add the wave to the data set */
    for (size_t i = 0; ret == 0 && i < nb_in_wave; i++) {
        ret = quicrq_relay_add_frame_to_cache(cached_media, wave[i], data, data_length);
        if (ret != 0) {
            DBG_PRINTF("Failure when adding frame %" PRIu64 " to cache", wave[i]);
        }
    }
    /* Check that the expected frame ids are returned */
    for (size_t i = 0; ret == 0 && i < nb_in_wave; i++) {
        uint64_t next_frame_id = UINT64_MAX;
        int f_ret = quicrq_relay_next_available_frame(frame_ranges, cached_media, &next_frame_id);
        if (f_ret != 0 || next_frame_id != wave[i]) {
            DBG_PRINTF("Expected frame_id %" PRIu64 ", got ret=%d, frame_id=%" PRIu64, wave[i], f_ret, next_frame_id);
            ret = -1;
        }
        else {
            ret = quicrq_relay_add_frame_id_to_ranges(frame_ranges, next_frame_id);
            if (ret != 0) {
                DBG_PRINTF("Failure when adding frame %" PRIu64 " to ranges", next_frame_id);
            }
        }
    }

    if (ret == 0) {
        uint64_t next_frame_id = UINT64_MAX;
        int f_ret = quicrq_relay_next_available_frame(frame_ranges, cached_media, &next_frame_id);
        if (f_ret == 0) {
            DBG_PRINTF("Expected no frame, got ret=0, frame_id=%" PRIu64, next_frame_id);
            ret = -1;
        }
    }

    return ret;
}

int quick_relay_range_test()
{
    int ret = 0;
    uint64_t wave1[] = { 3, 4, 6, 7, 10, 15 };
    uint64_t wave2[] = { 2, 8, 17 };
    uint64_t wave3[] = { 0, 1, 5, 9, 11, 12, 13, 14, 16, 18 };
    quicrq_sent_frame_ranges_t frame_ranges = { 0 };
    struct st_quicrq_relay_cached_media_t* cache_ctx = quicrq_relay_create_cache_ctx();

    if (cache_ctx == NULL) {
        ret = -1;
    }

    if (ret == 0) {
        ret = quick_relay_range_test_wave(&frame_ranges, cache_ctx, wave1, sizeof(wave1) / sizeof(uint64_t));
        if (ret != 0) {
            DBG_PRINTF("Relay range test fails after wave %d", 1);
        }
    }
    if (ret == 0) {
        ret = quick_relay_range_test_wave(&frame_ranges, cache_ctx, wave2, sizeof(wave2) / sizeof(uint64_t));
        if (ret != 0) {
            DBG_PRINTF("Relay range test fails after wave %d", 2);
        }
    }

    if (ret == 0) {
        ret = quick_relay_range_test_wave(&frame_ranges, cache_ctx, wave3, sizeof(wave3) / sizeof(uint64_t));
        if (ret != 0) {
            DBG_PRINTF("Relay range test fails after wave %d", 3);
        }
    }

    if (cache_ctx != NULL) {
        quicrq_relay_delete_cache_ctx(cache_ctx);
    }

    quick_relay_clear_ranges(&frame_ranges);

    return ret;
}