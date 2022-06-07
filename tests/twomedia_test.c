
#include <string.h>
#include "picoquic_set_textlog.h"
#include "picoquic_set_binlog.h"
#include "quicrq.h"
#include "quicrq_relay.h"
#include "quicrq_internal.h"
#include "quicrq_test_internal.h"

/* Two media test. 
 * very similar to the basic test, but getting two streams, 
 */

quicrq_test_config_t* quicrq_test_two_media_config_create(uint64_t simulate_loss, uint64_t extra_delay)
{
    /* Create a configuration with just two nodes, two links, one source and two attachment points.*/
    quicrq_test_config_t* config = quicrq_test_config_create(2, 2, 2, 2);

    if (config != NULL) {
        /* Create the contexts for the origin and the client */
        config->nodes[0] = quicrq_create(QUICRQ_ALPN,
            config->test_server_cert_file, config->test_server_key_file, NULL, NULL, NULL,
            config->ticket_encryption_key, sizeof(config->ticket_encryption_key),
            &config->simulated_time);
        config->nodes[1] = quicrq_create(QUICRQ_ALPN,
            NULL, NULL, config->test_server_cert_store_file, NULL, NULL,
            NULL, 0, &config->simulated_time);
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
        /* set the extra delays */
        for (int i = 0; i < config->nb_nodes; i++) {
            if (extra_delay > 0) {
                quicrq_set_extra_repeat(config->nodes[i], 1, 1);
            }
            quicrq_set_extra_repeat_delay(config->nodes[i], extra_delay);
        }
    }
    return config;
}

#if 0
/* TODO: test two media. */
/* Basic connection test */
int quicrq_two_media_test_one(int is_real_time, int use_datagrams, uint64_t simulate_losses, int is_from_client, int min_packet_size, uint64_t extra_delay)
{
    int ret = 0;
    int nb_steps = 0;
    int nb_inactive = 0;
    int is_closed = 0;
    const uint64_t max_time = 360000000;
    const int max_inactive = 128;
    quicrq_test_config_t* config = quicrq_test_basic_config_create(simulate_losses, extra_delay);
    quicrq_cnx_ctx_t* cnx_ctx = NULL;
    char media_source_path[512];
    char result_file_name[512];
    char result_log_name[512];
    char text_log_name[512];
    size_t nb_log_chars = 0;

    (void)picoquic_sprintf(text_log_name, sizeof(text_log_name), &nb_log_chars, "basic_textlog-%d-%d-%d-%llx-%zx-%llu.txt", is_real_time, use_datagrams, is_from_client,
        (unsigned long long)simulate_losses, min_packet_size, (unsigned long long)extra_delay);
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

        config->object_sources[0] = test_media_object_source_publish(config->nodes[publish_node], (uint8_t*)QUICRQ_TEST_BASIC_SOURCE,
            strlen(QUICRQ_TEST_BASIC_SOURCE), media_source_path, NULL, is_real_time, config->simulated_time);
        if (config->object_sources[0] == NULL) {
            ret = -1;
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
            if (ret == 0) {
                test_object_stream_ctx_t* object_stream_ctx = NULL;
                object_stream_ctx = test_object_stream_subscribe(cnx_ctx, (uint8_t*)QUICRQ_TEST_BASIC_SOURCE,
                    strlen(QUICRQ_TEST_BASIC_SOURCE), use_datagrams, result_file_name, result_log_name);
                if (object_stream_ctx == NULL) {
                    ret = -1;
                }
            }
        }
    }

    while (ret == 0 && nb_inactive < max_inactive && config->simulated_time < max_time) {
        /* Run the simulation. Monitor the connection. Monitor the media. */
        int is_active = 0;

        ret = quicrq_test_loop_step(config, &is_active, UINT64_MAX);
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
        }
        else {
            int client_stream_closed = config->nodes[1]->first_cnx->first_stream == NULL;
            int server_stream_closed = config->nodes[0]->first_cnx != NULL && config->nodes[0]->first_cnx->first_stream == NULL;

            if (!is_closed && client_stream_closed && server_stream_closed) {
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
#endif