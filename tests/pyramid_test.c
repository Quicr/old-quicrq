#include <string.h>
#include "picoquic_set_textlog.h"
#include "picoquic_set_binlog.h"
#include "quicrq.h"
#include "quicrq_relay.h"
#include "quicrq_internal.h"
#include "quicrq_test_internal.h"

/* Create a test network */
quicrq_test_config_t* quicrq_test_pyramid_config_create(uint64_t simulate_loss)
{
    /* Create a configuration with four nodes, five links, one source and 10 attachment points.*/
    quicrq_test_config_t* config = quicrq_test_config_create(4, 6, 6, 1);
    if (config != NULL) {
        /* Create the contexts for the origin (0),  relay (1) and client (2) */
        config->nodes[0] = quicrq_create(QUICRQ_ALPN,
            config->test_server_cert_file, config->test_server_key_file, NULL, NULL, NULL,
            config->ticket_encryption_key, sizeof(config->ticket_encryption_key),
            &config->simulated_time);
        config->nodes[1] = quicrq_create(QUICRQ_ALPN,
            config->test_server_cert_file, config->test_server_key_file, NULL, NULL, NULL,
            config->ticket_encryption_key, sizeof(config->ticket_encryption_key),
            &config->simulated_time);
        config->nodes[2] = quicrq_create(QUICRQ_ALPN,
            NULL, NULL, config->test_server_cert_store_file, NULL, NULL,
            NULL, 0, &config->simulated_time);
        config->nodes[3] = quicrq_create(QUICRQ_ALPN,
            NULL, NULL, config->test_server_cert_store_file, NULL, NULL,
            NULL, 0, &config->simulated_time);
        if (config->nodes[0] == NULL || config->nodes[1] == NULL || config->nodes[2] == NULL || config->nodes[3] == NULL) {
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
        config->attachments[2].node_id = 1;
        config->return_links[3] = 2;
        config->attachments[3].link_id = 3;
        config->attachments[3].node_id = 2;
        config->return_links[4] = 5;
        config->attachments[4].link_id = 4;
        config->attachments[4].node_id = 0;
        config->return_links[5] = 4;
        config->attachments[5].link_id = 5;
        config->attachments[5].node_id = 3;
        /* Set the desired loss pattern */
        config->simulate_loss = simulate_loss;
    }
    return config;
}

/* Basic relay test */
int quicrq_pyramid_testone(int is_real_time, int use_datagrams, uint64_t simulate_losses, int is_from_relay_client, uint64_t client_start_delay, uint64_t publish_start_delay)
{
    int ret = 0;
    int nb_steps = 0;
    int nb_inactive = 0;
    int is_closed = 0;
    int is_publisher_closed = 0;
    const uint64_t max_time = 360000000;
    const int max_inactive = 128;
    quicrq_test_config_t* config = quicrq_test_pyramid_config_create(simulate_losses);
    quicrq_cnx_ctx_t* cnx_ctx_relay = NULL;
    quicrq_cnx_ctx_t* cnx_ctx_server = NULL;
    int receive_node_id = (is_from_relay_client) ? 3 : 2;
    int publish_node_id = (is_from_relay_client) ? 2 : 3;
    char media_source_path[512];
    char result_file_name[512];
    char result_log_name[512];
    char text_log_name[512];
    size_t nb_log_chars = 0;
    uint64_t client_start_time = client_start_delay;
    uint64_t publisher_start_time = publish_start_delay;
    uint64_t app_wake_time = (client_start_delay> publish_start_delay)? publish_start_delay: client_start_delay;
    uint64_t is_client_started = 0;
    uint64_t is_publisher_started = 0;

    (void)picoquic_sprintf(text_log_name, sizeof(text_log_name), &nb_log_chars, "pyramid_textlog-%d-%d-%d-%llu-%llu-%llu.txt",
        is_real_time, use_datagrams, is_from_relay_client, (unsigned long long)simulate_losses,
        (unsigned long long)client_start_delay, (unsigned long long)publish_start_delay);
    ret = test_media_derive_file_names((uint8_t*)QUICRQ_TEST_BASIC_SOURCE, strlen(QUICRQ_TEST_BASIC_SOURCE),
        use_datagrams, is_real_time, is_from_relay_client,
        result_file_name, result_log_name, sizeof(result_file_name));

    if (config == NULL) {
        ret = -1;
    }

    /* Locate the source and reference file */
    if (picoquic_get_input_path(media_source_path, sizeof(media_source_path),
        quicrq_test_solution_dir, QUICRQ_TEST_BASIC_SOURCE) != 0) {
        ret = -1;
    }

    /* Add QUIC level log for the relay node */
    if (ret == 0) {
        ret = picoquic_set_textlog(config->nodes[1]->quic, text_log_name);
    }

    if (ret == 0) {
        /* Add a test source to the configuration, and to the either the first client (behind relay) or the second (direct to origin) */

        config->object_sources[0] = test_media_object_source_publish(config->nodes[publish_node_id], (uint8_t*)QUICRQ_TEST_BASIC_SOURCE,
            strlen(QUICRQ_TEST_BASIC_SOURCE), media_source_path, NULL, is_real_time, config->simulated_time);
        if (config->object_sources[0] == NULL) {
            ret = -1;
        }
    }

    if (ret == 0) {
        /* Enable origin on node 0 */
        ret = quicrq_enable_origin(config->nodes[0], use_datagrams);
        if (ret != 0) {
            DBG_PRINTF("Cannot enable origin, ret = %d", ret);
        }
    }

    if (ret == 0) {
        /* Configure the relay: joint client-server as default source and default consumer */
        /* Configure the relay: set the server address */
        struct sockaddr* addr_to = quicrq_test_find_send_addr(config, 1, 0);
        ret = quicrq_enable_relay(config->nodes[1], NULL, addr_to, use_datagrams);
        if (ret != 0) {
            DBG_PRINTF("Cannot enable relay, ret = %d", ret);
        }
    }

    if (ret == 0) {
        /* Create a quirq connection context on relay client */
        cnx_ctx_relay = quicrq_test_create_client_cnx(config, 2, 1);
        if (cnx_ctx_relay == NULL) {
            ret = -1;
            DBG_PRINTF("Cannot create client connection to relay, ret = %d", ret);
        }
    }

    if (ret == 0) {
        /* Create a quirq connection context on direct client */
        cnx_ctx_server = quicrq_test_create_client_cnx(config, 3, 0);
        if (cnx_ctx_server == NULL) {
            ret = -1;
            DBG_PRINTF("Cannot create client connection, ret = %d", ret);
        }
    }


    while (ret == 0 && nb_inactive < max_inactive && config->simulated_time < max_time) {
        /* Run the simulation. Monitor the connection. Monitor the media. */
        int is_active = 0;

        if (!is_client_started && config->simulated_time >= client_start_time) {
            /* Create a subscription to the test source on client */
            quicrq_cnx_ctx_t* cnx_ctx_get = (is_from_relay_client) ? cnx_ctx_server : cnx_ctx_relay;
            if (ret == 0) {
                test_object_stream_ctx_t* object_stream_ctx = NULL;
                object_stream_ctx = test_object_stream_subscribe(cnx_ctx_get, (const uint8_t*)QUICRQ_TEST_BASIC_SOURCE,
                    strlen(QUICRQ_TEST_BASIC_SOURCE), use_datagrams, result_file_name, result_log_name);
                if (object_stream_ctx == NULL) {
                    ret = -1;
                }
            }
            if (ret != 0) {
                DBG_PRINTF("Cannot subscribe to test media %s, ret = %d", QUICRQ_TEST_BASIC_SOURCE, ret);
            }
            else {
                is_client_started = 1;

                app_wake_time = (is_publisher_started) ? max_time : publisher_start_time;
            }
        }

        if ((ret == 0) && !is_publisher_started && (config->simulated_time >= publisher_start_time)) {
            /* Start pushing from the publisher client */
            quicrq_cnx_ctx_t* cnx_ctx_post = (is_from_relay_client) ? cnx_ctx_relay : cnx_ctx_server;
            ret = quicrq_cnx_post_media(cnx_ctx_post, (uint8_t*)QUICRQ_TEST_BASIC_SOURCE, strlen(QUICRQ_TEST_BASIC_SOURCE), use_datagrams);
            if (ret != 0) {
                DBG_PRINTF("Cannot subscribe to test media %s, ret = %d", QUICRQ_TEST_BASIC_SOURCE, ret);
            }
            else {
                is_publisher_started = 1;
                app_wake_time = (is_client_started) ? max_time : client_start_time;
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
        /* if the media is received, exit the loop */
        if (config->nodes[receive_node_id]->first_cnx == NULL) {
            DBG_PRINTF("%s", "Exit loop after clients connection closed.");
            break;
        }
        else {
            int client_stream_closed = config->nodes[receive_node_id]->first_cnx == NULL || config->nodes[receive_node_id]->first_cnx->first_stream == NULL;
            int publish_stream_closed = config->nodes[publish_node_id]->first_cnx == NULL || config->nodes[publish_node_id]->first_cnx->first_stream == NULL;

            if (!is_closed && client_stream_closed && is_client_started) {
                if (config->nodes[receive_node_id]->first_cnx != NULL) {
                    /* Client is done. Close client connection without waiting for timer */
                    ret = picoquic_close(config->nodes[receive_node_id]->first_cnx->cnx, 0);
                    if (ret != 0) {
                        DBG_PRINTF("Cannot close client connection, ret = %d", ret);
                    }
                }
                is_closed = 1;
            }

            if (is_publisher_started && !is_publisher_closed && publish_stream_closed) {
                /* Publisher is done. Close publisher connection immediately, to test that receivers will
                 * still receive the last data even if the publisher is closed. */
                if (config->nodes[publish_node_id]->first_cnx != NULL) {
                    /* Client is done. Close client connection without waiting for timer */
                    ret = picoquic_close(config->nodes[publish_node_id]->first_cnx->cnx, 0);
                    if (ret != 0) {
                        DBG_PRINTF("Cannot close publisher connection, ret = %d", ret);
                    }
                }
                is_publisher_closed = 1;
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

int quicrq_pyramid_basic_test()
{
    int ret = quicrq_pyramid_testone(1, 0, 0, 0, 0, 0);

    return ret;
}

int quicrq_pyramid_datagram_test()
{
    int ret = quicrq_pyramid_testone(1, 1, 0, 0, 0, 0);

    return ret;
}

int quicrq_pyramid_datagram_loss_test()
{
    int ret = quicrq_pyramid_testone(1, 1, 0x7080, 0, 0, 0);

    return ret;
}

int quicrq_pyramid_basic_client_test()
{
    int ret = quicrq_pyramid_testone(1, 0, 0, 1, 0, 0);

    return ret;
}

int quicrq_pyramid_datagram_client_test()
{
    int ret = quicrq_pyramid_testone(1, 1, 0, 1, 0, 0);

    return ret;
}

int quicrq_pyramid_datagram_delay_test()
{
    int ret = quicrq_pyramid_testone(1, 1, 0, 1, 2000000, 0);

    return ret;
}

int quicrq_pyramid_publish_delay_test()
{
    int ret = quicrq_pyramid_testone(1, 1, 0, 1, 0, 2000000);

    return ret;
}



