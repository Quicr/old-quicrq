#include <string.h>
#include "picoquic_set_textlog.h"
#include "picoquic_set_binlog.h"
#include "quicrq.h"
#include "quicrq_relay.h"
#include "quicrq_internal.h"
#include "quicrq_test_internal.h"
#include "picoquic_internal.h"

/* two ways test
 * Test a "two ways" configuration, in which two clients communicate through a server.
 * Both clients post a media fragment, which the server caches.
 * Both clients get the media fragment.
 */

/* Create a test network */
quicrq_test_config_t* quicrq_test_twoways_config_create(uint64_t simulate_loss)
{
    /* Create a configuration with three nodes, four links, two source and 4 attachment points.*/
    quicrq_test_config_t* config = quicrq_test_config_create(3, 4, 4, 2);
    if (config != NULL) {
        /* Create the contexts for the origin (0),  client-1 (1) and client-2 (2) */
        config->nodes[0] = quicrq_create(QUICRQ_ALPN,
            config->test_server_cert_file, config->test_server_key_file, NULL, NULL, NULL,
            config->ticket_encryption_key, sizeof(config->ticket_encryption_key),
            &config->simulated_time);
        config->nodes[1] = quicrq_create(QUICRQ_ALPN,
            NULL, NULL, config->test_server_cert_store_file, NULL, NULL,
            NULL, 0, &config->simulated_time);
        config->nodes[2] = quicrq_create(QUICRQ_ALPN,
            NULL, NULL, config->test_server_cert_store_file, NULL, NULL,
            NULL, 0, &config->simulated_time);
        if (config->nodes[0] == NULL || config->nodes[1] == NULL || config->nodes[1] == NULL) {
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
        config->return_links[2] = 3;
        config->attachments[2].link_id = 2;
        config->attachments[2].node_id = 0;
        config->return_links[3] = 2;
        config->attachments[3].link_id = 3;
        config->attachments[3].node_id = 2;
        /* Set the desired loss pattern */
        config->simulate_loss = simulate_loss;
    }
    return config;
}

/* Symmetric triangle test
 * We want to test a couple of different configurations:
 * 
 * 0) One source per node, each node gets the data from the other node.
 * 1) Two sources on one node, the other gets the data.
 * 2) Same as 1, but the subscriber starts before the publisher.
 */
int quicrq_twoways_test_one(int is_real_time, quicrq_transport_mode_enum transport_mode, uint64_t simulate_losses, int test_mode)
{
    int ret = 0;
    int nb_steps = 0;
    int nb_inactive = 0;
    int is_closed = 0;
    const uint64_t max_time = 360000000;
    const int max_inactive = 128;
    quicrq_test_config_t* config = quicrq_test_twoways_config_create(simulate_losses);
    char media_source_path[512];
    char text_log_name[256];
    char test_id[256];
    size_t nb_log_chars = 0;
    int partial_closure = 0;
    uint64_t start_delay[2] = { 0, 1000000 };
    int client_is_started[2] = { 0, 0 };
    char const* url[2] = { "media1", "media2" };
    quicrq_cnx_ctx_t* cnx_ctx[2] = { NULL, NULL };
    quicrq_test_config_target_t* target[2] = { NULL, NULL };
    uint64_t client_close_time = UINT64_MAX;

    if (config == NULL) {
        ret = -1;
    }
    else {
        (void)picoquic_sprintf(test_id, sizeof(test_id), NULL, "twoways-%d-%c-%llx-%d", is_real_time, 
            quircq_transport_mode_to_letter(transport_mode),
            (unsigned long long)simulate_losses, test_mode);
        (void)picoquic_sprintf(text_log_name, sizeof(text_log_name), &nb_log_chars, "%s_textlog.txt", test_id);

        /* Locate the source and reference file */
        if (ret == 0 && picoquic_get_input_path(media_source_path, sizeof(media_source_path),
            quicrq_test_solution_dir, QUICRQ_TEST_BASIC_SOURCE) != 0) {
            ret = -1;
        }

        /* Create the required target references*/
        for (int i = 0; ret == 0 && i < 2; i++) {
            int client_id = (test_mode == 0) ? i + 1 : 1;
            target[i] = quicrq_test_config_target_create(test_id, url[i^1], client_id, media_source_path);
            if (target[i] == NULL) {
                DBG_PRINTF("Cannot create targets for target %d", i);
                ret = -1;
            }
        }
    }

    /* Add QUIC level log */
    if (ret == 0) {
        ret = picoquic_set_textlog(config->nodes[0]->quic, text_log_name);
    }

    if (ret == 0) {
        /* Enable origin on node 0 */
        ret = quicrq_enable_origin(config->nodes[0], transport_mode);
        if (ret != 0) {
            DBG_PRINTF("Cannot enable origin, ret = %d", ret);
        }
    }
    
    /* Publish sources either from both nodes if mode = 0 */
    if (test_mode == 0) {
        for (int source_id = 0; ret == 0 && source_id < 2; source_id++) {
            int publish_node = source_id + 1;
            config->object_sources[source_id] = test_media_object_source_publish(config->nodes[publish_node], (uint8_t*)url[source_id],
                strlen(url[source_id]), media_source_path, NULL, is_real_time, config->simulated_time);
            if (config->object_sources[source_id] == NULL) {
                ret = -1;
            }
        }
    }

    /* If mode 2, use reverse version of start delay */
    if (test_mode == 2) {
        start_delay[0] = 2500000;
        start_delay[1] = 0;
    }

    while (ret == 0 && nb_inactive < max_inactive && config->simulated_time < max_time) {
        /* Run the simulation. Monitor the connection. Monitor the media. */
        int is_active = 0;
        uint64_t app_wake_time = UINT64_MAX;
        for (int i = 0; ret == 0 && i < 2; i++) {
            if (!client_is_started[i]) {
                if (config->simulated_time >= start_delay[i]) {
                    /* Start client i */
                    int c_node_id = i + 1;

                    if (ret == 0) {
                        /* Create a quicrq connection context on client */
                        cnx_ctx[i] = quicrq_test_create_client_cnx(config, c_node_id, 0);
                        if (cnx_ctx[i] == NULL) {
                            ret = -1;
                            DBG_PRINTF("Cannot create client connection %d, ret = %d", c_node_id, ret);
                        }
                    }

                    if (test_mode == 0) {
                        if (ret == 0) {
                            /* Start pushing from the client */
                            ret = quicrq_cnx_post_media(cnx_ctx[i], (uint8_t*)url[i], strlen(url[i]), transport_mode);
                            if (ret != 0) {
                                DBG_PRINTF("Cannot publish test media %s, ret = %d", url[i], ret);
                            }
                        }

                        if (ret == 0) {
                            /* Create a subscription to the test source on other client*/
                            if (ret == 0) {
                                test_object_stream_ctx_t* object_stream_ctx = NULL;
                                object_stream_ctx = test_object_stream_subscribe(cnx_ctx[i], (const uint8_t*)target[i]->url,
                                    target[i]->url_length, transport_mode, target[i]->target_bin, target[i]->target_csv);
                                if (object_stream_ctx == NULL) {
                                    ret = -1;
                                }
                            }
                            if (ret != 0) {
                                DBG_PRINTF("Cannot subscribe to test media %s, ret = %d", target[i]->url, ret);
                            }
                        }
                    }
                    else {
                        if (i == 0) {
                            /* This is the receiving client */
                            for (int source_id = 0; ret == 0 && source_id < 2; source_id++) {
                                /* Create a subscription to the test source on client*/
                                test_object_stream_ctx_t* object_stream_ctx = NULL;
                                quicrq_subscribe_intent_t intent = { quicrq_subscribe_intent_current_group, 0, 0 };
                                object_stream_ctx = test_object_stream_subscribe_ex(cnx_ctx[i], (const uint8_t*)target[source_id]->url,
                                    target[source_id]->url_length, transport_mode, &intent, target[source_id]->target_bin, target[source_id]->target_csv);
                                if (object_stream_ctx == NULL) {
                                    ret = -1;
                                }
                            }
                        }
                        else {
                            /* This is the sending client */
                            for (int source_id = 0; ret == 0 && source_id < 2; source_id++) {
                                /* Create a source on the publisher */
                                int publish_node = 2;
                                quicrq_media_object_source_properties_t properties = { 1, 0, 0 };
                                config->object_sources[source_id] = test_media_object_source_publish_ex(config->nodes[publish_node], (uint8_t*)url[source_id],
                                    strlen(url[source_id]), media_source_path, NULL, is_real_time, config->simulated_time, &properties);
                                if (config->object_sources[source_id] == NULL) {
                                    DBG_PRINTF("Cannot publish test media %s, ret = %d", url[source_id], ret);
                                    ret = -1;
                                }
                                else {
                                    /* Start pushing from the client */
                                    ret = quicrq_cnx_post_media(cnx_ctx[i], (uint8_t*)url[source_id], strlen(url[source_id]), transport_mode);
                                    if (ret != 0) {
                                        DBG_PRINTF("Cannot publish test media %s, ret = %d", url[source_id], ret);
                                    }
                                }
                            }
                        }
                    }
                    client_is_started[i] = 1;
                }
                else if (start_delay[i] < app_wake_time) {
                    app_wake_time = start_delay[i];
                }
            }
        }

        ret = quicrq_test_loop_step(config, &is_active, app_wake_time);
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
        if (config->nodes[1]->first_cnx == NULL && config->nodes[2]->first_cnx == NULL) {
            DBG_PRINTF("%s", "Exit loop after client connection #2 closed.");
            break;
        }
        else {
            /* TODO: add closing condition on server */
            int client1_stream_closed = client_is_started[0] && (config->nodes[1]->first_cnx == NULL || config->nodes[1]->first_cnx->first_stream == NULL);
            int client2_stream_closed = client_is_started[1] && (config->nodes[2]->first_cnx == NULL || config->nodes[2]->first_cnx->first_stream == NULL);

            if (!is_closed && client1_stream_closed && client2_stream_closed) {
                /* Clients are done. Close connections without waiting for timer -- if not closed yet */
                is_closed = 1;
                client_close_time = config->simulated_time;
                for (int c_nb = 1; ret == 0 && c_nb < 3; c_nb++) {
                    if (config->nodes[c_nb]->first_cnx != NULL) {
                        ret = quicrq_close_cnx(config->nodes[c_nb]->first_cnx);
                        if (ret != 0) {
                            DBG_PRINTF("Cannot close client connection, ret = %d", ret);
                        }
                    }
                }
            }
            else if (client1_stream_closed ^ client2_stream_closed) {
                if (!partial_closure) {
                    partial_closure = 1;
                    DBG_PRINTF("Partial closure: client 1 (%d), client 2 (%d), time = %" PRIu64, 
                        client1_stream_closed, client2_stream_closed, config->simulated_time);
                }
            }
        }
    }

    if (ret == 0 && (!is_closed || client_close_time > 12000000)) {
        DBG_PRINTF("Session was not properly closed, time = %" PRIu64, client_close_time);
        ret = -1;
    }

    /* Verify that media file was received correctly */

    if (ret != 0) {
        DBG_PRINTF("Test failed before getting results, ret = %d", ret);
    }
    else {
        for (int i = 0; ret == 0 && i < 2; i++) {
            if (test_mode == 2) {
                /* For that test, expect transmission to start at GOB=1 */
                int nb_losses = 0;
                uint8_t loss_flag = 0;
                ret = quicrq_compare_media_file_ex(target[i]->target_bin, target[i]->ref, &nb_losses, &loss_flag, 1, 0);
            }
            else {
                ret = quicrq_compare_media_file(target[i]->target_bin, target[i]->ref);
            }
        }
    }

    /* Clear everything. */
    for (int i = 0; i < 2; i++) {
        if (target[i] != NULL) {
            quicrq_test_config_target_free(target[i]);
            target[i] = NULL;
        }
    }
    if (config != NULL) {
        quicrq_test_config_delete(config);
    }

    return ret;
}

int quicrq_twoways_basic_test()
{
    int ret = quicrq_twoways_test_one(1, quicrq_transport_mode_single_stream, 0, 0);

    return ret;
}

int quicrq_twoways_datagram_test()
{
    int ret = quicrq_twoways_test_one(1, quicrq_transport_mode_datagram, 0, 0);

    return ret;
}

int quicrq_twoways_datagram_loss_test()
{
    int ret = quicrq_twoways_test_one(1, quicrq_transport_mode_datagram, 0x7080, 0);

    return ret;
}

int quicrq_twomedia_tri_stream_test()
{
    int ret = quicrq_twoways_test_one(1, quicrq_transport_mode_single_stream, 0, 1);

    return ret;
}

int quicrq_twomedia_tri_datagram_test()
{
    int ret = quicrq_twoways_test_one(1, quicrq_transport_mode_datagram, 0, 1);

    return ret;
}

int quicrq_twomedia_tri_later_test()
{
    int ret = quicrq_twoways_test_one(1, quicrq_transport_mode_datagram, 0, 2);

    return ret;
}