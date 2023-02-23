#include <string.h>
#include "picoquic_set_textlog.h"
#include "picoquic_set_binlog.h"
#include "quicrq.h"
#include "quicrq_relay.h"
#include "quicrq_internal.h"
#include "quicrq_test_internal.h"
#include "quicrq_fragment.h"

/* Four legs test:
 * Four clients, attached to two relays, one of the clients is the publisher.
 * The configuration diagram is:
 * 
 *             S
 *            / \
 *           /   \
 *          /     \
 *         R1      R2
 *        / \     / \
 *       C2  C2  C3  C4(publisher)
 *      
 */

/* Create a test network */
quicrq_test_config_t* quicrq_test_fourlegs_config_create(uint64_t simulate_loss)
{
    /* Create a configuration with seven nodes, twelve links, one source and 12 attachment points.*/
    quicrq_test_config_t* config = quicrq_test_config_create(7, 12, 12, 1);
    if (config != NULL) {
        /* Create the contexts for the origin (0),  relay (1), relay(2), and client-1 (3),
         * client-2 (4), client-3 (5) and client-3(4) */
        config->nodes[0] = quicrq_create(QUICRQ_ALPN,
            config->test_server_cert_file, config->test_server_key_file, NULL, NULL, NULL,
            config->ticket_encryption_key, sizeof(config->ticket_encryption_key),
            &config->simulated_time);
        config->nodes[1] = quicrq_create(QUICRQ_ALPN,
            config->test_server_cert_file, config->test_server_key_file, NULL, NULL, NULL,
            config->ticket_encryption_key, sizeof(config->ticket_encryption_key),
            &config->simulated_time);
        config->nodes[2] = quicrq_create(QUICRQ_ALPN,
            config->test_server_cert_file, config->test_server_key_file, NULL, NULL, NULL,
            config->ticket_encryption_key, sizeof(config->ticket_encryption_key),
            &config->simulated_time);
        config->nodes[3] = quicrq_create(QUICRQ_ALPN,
            NULL, NULL, config->test_server_cert_store_file, NULL, NULL,
            NULL, 0, &config->simulated_time);
        config->nodes[4] = quicrq_create(QUICRQ_ALPN,
            NULL, NULL, config->test_server_cert_store_file, NULL, NULL,
            NULL, 0, &config->simulated_time);
        config->nodes[5] = quicrq_create(QUICRQ_ALPN,
            NULL, NULL, config->test_server_cert_store_file, NULL, NULL,
            NULL, 0, &config->simulated_time);
        config->nodes[6] = quicrq_create(QUICRQ_ALPN,
            NULL, NULL, config->test_server_cert_store_file, NULL, NULL,
            NULL, 0, &config->simulated_time);
        for (int i = 0; i < 7; i++) {
            if (config->nodes[i] == NULL) {
                quicrq_test_config_delete(config);
                config = NULL;
                break;
            }
        }
    }
    if (config != NULL) {
        /* Populate the links and attachments */
        /* S to R1: links 0 and 1 */
        config->return_links[0] = 1;
        config->attachments[0].link_id = 0;
        config->attachments[0].node_id = 0;
        config->return_links[1] = 0;
        config->attachments[1].link_id = 1;
        config->attachments[1].node_id = 1;
        /* S to R2: links 2 and 3 */
        config->return_links[2] = 3;
        config->attachments[2].link_id = 2;
        config->attachments[2].node_id = 0;
        config->return_links[3] = 2;
        config->attachments[3].link_id = 3;
        config->attachments[3].node_id = 2;
        /* R1 to C1: links 4 and 5 */
        config->return_links[4] = 5;
        config->attachments[4].link_id = 4;
        config->attachments[4].node_id = 1;
        config->return_links[5] = 4;
        config->attachments[5].link_id = 5;
        config->attachments[5].node_id = 3;
        /* R1 to C2: links 6 and 7 */
        config->return_links[6] = 7;
        config->attachments[6].link_id = 6;
        config->attachments[6].node_id = 1;
        config->return_links[7] = 6;
        config->attachments[7].link_id = 7;
        config->attachments[7].node_id = 4;
        /* R2 to C3: links 8 and 9 */
        config->return_links[8] = 9;
        config->attachments[8].link_id = 8;
        config->attachments[8].node_id = 2;
        config->return_links[9] = 8;
        config->attachments[9].link_id = 9;
        config->attachments[9].node_id = 5;
        /* R2 to C4: links 10 and 11 */
        config->return_links[10] = 11;
        config->attachments[10].link_id = 10;
        config->attachments[10].node_id = 2;
        config->return_links[11] = 10;
        config->attachments[11].link_id = 11;
        config->attachments[11].node_id = 6;
        /* Set the desired loss pattern */
        config->simulate_loss = simulate_loss;
    }
    return config;
}

/* For debugging: print the state of a data source.
 * This is only used during tests, after modifying the code.
 */
void quicrq_debug_source_test(quicrq_ctx_t* node, int node_id)
{
    quicrq_media_source_ctx_t* source = node->first_source;
    int source_id = 0;
    
    while (source != NULL) {
        quicrq_fragment_cache_t* cache_ctx = source->cache_ctx;
        if (cache_ctx == NULL) {
            DBG_PRINTF("No cache for node{%d], source[%d]", node_id, source_id);
        } else {
            quicrq_cached_fragment_t* fragment = (quicrq_cached_fragment_t*)quicrq_fragment_cache_node_value(picosplay_first(&source->cache_ctx->fragment_tree));

            if (fragment == NULL) {
                DBG_PRINTF("Empty cache for node{%d], source[%d]", node_id, source_id);
            }
            else {
                int ret = 0;
                uint64_t current_group_id = fragment->group_id;
                uint64_t current_object_id = fragment->object_id;
                uint64_t current_offset = 0;
                int is_new_object = 1;
                int is_last_fragment = 0;

                if (current_group_id != cache_ctx->first_group_id || current_object_id != cache_ctx->first_object_id) {
                    DBG_PRINTF("Cache[%d,%d] starts a %" PRIu64 "/%" PRIu64 " vs %" PRIu64 "/%" PRIu64, node_id, source_id,
                        current_group_id, current_object_id, cache_ctx->first_group_id, cache_ctx->first_object_id);
                }
                while (fragment != NULL) {
                    if (fragment->offset != current_offset) {
                        DBG_PRINTF("Cache[%d,%d] object %" PRIu64 "/%" PRIu64 " offset %zu instead of %zu", node_id, source_id,
                            current_group_id, current_object_id, fragment->offset, current_offset);
                        ret = -1;
                        break;
                    }
                    current_offset += fragment->data_length;
                    is_last_fragment = current_offset >= fragment->object_length;
                    fragment = (quicrq_cached_fragment_t*)quicrq_fragment_cache_node_value(picosplay_next(&fragment->fragment_node));
                    if (fragment == NULL){
                        if (!is_last_fragment) {
                            DBG_PRINTF("Cache[%d,%d] last object %" PRIu64 "/%" PRIu64 " offset %zu, incomplete", node_id, source_id,
                                current_group_id, current_object_id, current_offset);
                            ret = -1;
                        }
                        break;
                    }
                    if (fragment->group_id != current_group_id || fragment->object_id != current_object_id){
                        if (!is_last_fragment) {
                            DBG_PRINTF("Cache[%d,%d] object %" PRIu64 "/%" PRIu64 " offset %zu, incomplete", node_id, source_id,
                                current_group_id, current_object_id, current_offset);
                            ret = -1;
                        }
                        if (fragment->group_id == current_group_id){
                            if (fragment->object_id != current_object_id + 1) {
                                DBG_PRINTF("Cache[%d,%d] missing object %" PRIu64 "/%" PRIu64, node_id, source_id,
                                    current_group_id, current_object_id + 1);
                                ret = -1;
                                break;
                            }
                        }
                        else if (fragment->group_id == current_group_id + 1) {
                            if (current_object_id + 1 != fragment->nb_objects_previous_group) {
                                DBG_PRINTF("Cache[%d,%d] missing object %" PRIu64 "/%" PRIu64, node_id, source_id,
                                    current_group_id, fragment->nb_objects_previous_group);
                                ret = -1;
                                break;
                            }
                            else if (fragment->object_id != 0) {
                                DBG_PRINTF("Cache[%d,%d] missing object %" PRIu64 "/%" PRIu64, node_id, source_id,
                                    current_group_id + 1, 0);
                                ret = -1;
                                break;
                            }
                        }
                        else {
                            DBG_PRINTF("Cache[%d,%d] missing object %" PRIu64 "/%" PRIu64, node_id, source_id,
                                current_group_id + 1, 0);
                            ret = -1;
                            break;
                        }
                        current_group_id = fragment->group_id;
                        current_object_id = fragment->object_id;
                        current_offset = 0;
                    }
                }
                if (ret == 0) {
                    if (cache_ctx->final_group_id == 0 && cache_ctx->final_object_id == 0) {
                        DBG_PRINTF("Cache[%d,%d] final object not yet known.", node_id, source_id);
                        ret = -1;
                    }
                    else if (current_group_id != cache_ctx->final_group_id && current_object_id + 1 != cache_ctx->final_object_id) {
                        DBG_PRINTF("Cache[%d,%d] missing last object before %" PRIu64 "/%" PRIu64, node_id, source_id,
                            cache_ctx->final_group_id, cache_ctx->final_object_id);
                        ret = -1;
                    }
                    else if (!is_last_fragment) {
                        DBG_PRINTF("Cache[%d,%d] last object incomplete before %" PRIu64 "/%" PRIu64, node_id, source_id,
                            cache_ctx->final_group_id, cache_ctx->final_object_id);
                        ret = -1;
                    }
                }
            }
        }
        source_id++;
        source = source->next_source;
    }
}

/* Four legs tests: One origin, two relays, 3 receivers and one sender. */
int quicrq_fourlegs_test_one(quicrq_transport_mode_enum transport_mode, uint64_t simulate_losses, int publish_last)
{
    int ret = 0;
    int nb_steps = 0;
    int nb_inactive = 0;
    int is_closed = 0;
    const uint64_t max_time = 360000000;
    const int max_inactive = 128;
    quicrq_test_config_t* config = quicrq_test_fourlegs_config_create(simulate_losses);
    char media_source_path[512];
    char result_file_name_1[256];
    char result_file_name_2[256];
    char result_file_name_3[256];
    char result_log_name_1[256];
    char result_log_name_2[256];
    char result_log_name_3[256];
    char* result_file_name[3] = { result_file_name_1, result_file_name_2, result_file_name_3 };
    char* result_log_name[3] = { result_log_name_1, result_log_name_2, result_log_name_3 };

    char text_log_name[512];
    size_t nb_log_chars = 0;
    uint64_t start_delay[4] = { 0, 3000000, 2000000, 1000000};
    int client_is_started[4] = { 0, 0, 0, 0 };
    quicrq_cnx_ctx_t* cnx_ctx[4] = { NULL, NULL, NULL, NULL };
    int partial_closure = 0;

    if (publish_last) {
        /* test what happens if local client subscribes before 
         * data is pushed to local relay. */
        start_delay[2] = 1000000;
        start_delay[3] = 2000000;
    }

    (void)picoquic_sprintf(text_log_name, sizeof(text_log_name), &nb_log_chars, "fourlegs_textlog-%c-%llx-%d.txt",
        quicrq_transport_mode_to_letter(transport_mode), (unsigned long long)simulate_losses, publish_last);
    for (int i = 0; i < 3; i++) {
        (void)picoquic_sprintf(result_file_name[i], sizeof(result_file_name_1), &nb_log_chars, "fourlegs-video1-recv-%d-%c-%llx-%d.bin",
            i+1, quicrq_transport_mode_to_letter(transport_mode), (unsigned long long)simulate_losses, publish_last);
        (void)picoquic_sprintf(result_log_name[i], sizeof(result_log_name_1), &nb_log_chars, "fourlegs-video1-log-%d-%c-%llx-%d.csv",
            i+1, quicrq_transport_mode_to_letter(transport_mode), (unsigned long long)simulate_losses, publish_last);
    }

    if (config == NULL) {
        ret = -1;
    }

    /* Locate the source and reference file */
    if (picoquic_get_input_path(media_source_path, sizeof(media_source_path),
        quicrq_test_solution_dir, QUICRQ_TEST_BASIC_SOURCE) != 0) {
        ret = -1;
    }

    /* Add QUIC level log to the second relay */
    if (ret == 0) {
        ret = picoquic_set_textlog(config->nodes[2]->quic, text_log_name);
    }

    if (ret == 0) {
        /* Enable origin on node 0 */
        ret = quicrq_enable_origin(config->nodes[0], transport_mode);
        if (ret != 0) {
            DBG_PRINTF("Cannot enable origin, ret = %d", ret);
        }
    }

    if (ret == 0) {
        /* test source is always client # 4 (node #6) */
        int publish_node = 6;

        config->object_sources[0] = test_media_object_source_publish(config->nodes[publish_node], (uint8_t*)QUICRQ_TEST_BASIC_SOURCE,
            strlen(QUICRQ_TEST_BASIC_SOURCE), media_source_path, NULL, 1, config->simulated_time);
        if (config->object_sources[0] == NULL) {
            ret = -1;
        }
    }

    for (int i_relay = 1; ret == 0 && i_relay <= 2; i_relay++) {
        /* Configure the relays: joint client-server as default source and default consumer */
        /* Configure the relays: set the server address */
        struct sockaddr* addr_to = quicrq_test_find_send_addr(config, i_relay, 0);
        ret = quicrq_enable_relay(config->nodes[i_relay], NULL, addr_to, transport_mode);
        if (ret != 0) {
            DBG_PRINTF("Cannot enable relay %s, ret = %d", i_relay, ret);
        }
    }

    while (ret == 0 && nb_inactive < max_inactive && config->simulated_time < max_time) {
        /* Run the simulation. Monitor the connection. Monitor the media. */
        int is_active = 0;
        uint64_t app_wake_time = UINT64_MAX;
        for (int i = 0; ret == 0 && i < 4; i++) {
            if (!client_is_started[i]) {
                if (config->simulated_time >= start_delay[i]) {
                    /* Start client i */
                    int c_node_id = i + 3;
                    int target_id = (i < 2) ? 1 : 2;

                    if (ret == 0) {
                        /* Create a quicrq connection context on client */
                        cnx_ctx[i] = quicrq_test_create_client_cnx(config, c_node_id, target_id);
                        if (cnx_ctx[i] == NULL) {
                            ret = -1;
                            DBG_PRINTF("Cannot create client connection %d, ret = %d", c_node_id, ret);
                        }
                    }
                    if (ret == 0) {
                        if (i == 3) {
                            /* Start pushing from client 4, node 6 */
                            ret = quicrq_cnx_post_media(cnx_ctx[i], (uint8_t*)QUICRQ_TEST_BASIC_SOURCE, strlen(QUICRQ_TEST_BASIC_SOURCE), transport_mode);
                            if (ret != 0) {
                                DBG_PRINTF("Cannot publish test media %s, ret = %d", QUICRQ_TEST_BASIC_SOURCE, ret);
                            }
                        }
                        else {
                            /* Create a subscription to the test source on other clients */
                            if (ret == 0) {
                                test_object_stream_ctx_t* object_stream_ctx = NULL;

                                object_stream_ctx = test_object_stream_subscribe(cnx_ctx[i], (const uint8_t*)QUICRQ_TEST_BASIC_SOURCE,
                                    strlen(QUICRQ_TEST_BASIC_SOURCE), transport_mode, result_file_name[i], result_log_name[i]);
                                if (object_stream_ctx == NULL) {
                                    ret = -1;
                                }
                            }
                        }
                        client_is_started[i] = 1;
                    }
                }
                else if (start_delay[i] < app_wake_time) {
                    app_wake_time = start_delay[i];
                }
            }
        }
        if (ret == 0) {
            ret = quicrq_test_loop_step(config, &is_active, app_wake_time);
        }
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

        /* if the media is sent and received, exit the loop */
        if (ret == 0) {
            int all_closed = 1;
            int all_done = 1;
            int nb_done = 0;
            int is_client_closed[4] = { 0, 0, 0, 0 };
            int is_client_done[4] = { 0, 0, 0, 0 };

            for (int i = 0; i < 4; i++) {
                int node_id = i + 3;
                is_client_closed[i] = client_is_started[i] && config->nodes[node_id]->first_cnx == NULL;
                is_client_done[i] = client_is_started[i] && (config->nodes[node_id]->first_cnx == NULL || config->nodes[node_id]->first_cnx->first_stream == NULL);
                all_closed &= is_client_closed[i];
                all_done &= is_client_done[i];
                nb_done += is_client_done[i];
            }

            if (all_closed) {
                DBG_PRINTF("Exit loop after all client connections closed, t=%" PRIu64, config->simulated_time);
                break;
            }
            else if (!is_closed) {
                if (all_done) {
                    /* Clients are done. Close connections without waiting for timer -- if not closed yet */
                    is_closed = 1;
                    for (int c_nb = 3; ret == 0 && c_nb < 7; c_nb++) {
                        if (config->nodes[c_nb]->first_cnx != NULL) {
                            ret = quicrq_close_cnx(config->nodes[c_nb]->first_cnx);
                            if (ret != 0) {
                                DBG_PRINTF("Cannot close client connection, ret = %d", ret);
                            }
                        }
                    }
                }
                else if (nb_done > 0) {
                    if (partial_closure < nb_done) {
                        partial_closure = nb_done;
                        DBG_PRINTF("Partial closure: client 1 (%d), client 2 (%d), client 3 (%d), client 4 (%d), time = %" PRIu64,
                            is_client_done[0], is_client_done[1], is_client_done[2], is_client_done[3], config->simulated_time);
                    }
                }
            }
        }
    }

    if (ret == 0 && (!is_closed || config->simulated_time > 12000000)) {
        DBG_PRINTF("Session was not properly closed, time = %" PRIu64, config->simulated_time);
        ret = -1;
    }

    /* Verify that media file was received correctly */
    if (ret == 0) {
        for (int i = 0; ret == 0 && i < 3; i++) {
            ret = quicrq_compare_media_file(result_file_name[i], media_source_path);
        }
    }
    else {
        DBG_PRINTF("Test failed before getting results, ret = %d", ret);
    }

    /* Clear everything. */
    if (config != NULL) {
        quicrq_test_config_delete(config);
    }

    return ret;
}

int quicrq_fourlegs_basic_test()
{
    int ret = quicrq_fourlegs_test_one(quicrq_transport_mode_single_stream, 0, 0);

    return ret;
}

int quicrq_fourlegs_basic_last_test()
{
    int ret = quicrq_fourlegs_test_one(quicrq_transport_mode_single_stream, 0, 1);

    return ret;
}

int quicrq_fourlegs_datagram_test()
{
    int ret = quicrq_fourlegs_test_one(quicrq_transport_mode_datagram, 0, 0);

    return ret;
}

int quicrq_fourlegs_datagram_last_test()
{
    int ret = quicrq_fourlegs_test_one(quicrq_transport_mode_datagram, 0, 1);

    return ret;
}

int quicrq_fourlegs_datagram_loss_test()
{
    int ret = quicrq_fourlegs_test_one(quicrq_transport_mode_datagram, 0x37880, 0);

    return ret;
}
