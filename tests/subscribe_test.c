#include <string.h>
#include <stdlib.h>
#include "picoquic_set_textlog.h"
#include "picoquic_set_binlog.h"
#include "quicrq.h"
#include "quicrq_relay.h"
#include "quicrq_internal.h"
#include "quicrq_test_internal.h"

/* Subscribe test
 * Test the "subscribe to prefix" function, using a set of scenarios:
 * 1) Simplest: client-1 subscribe to source on origin.
 * 2) Triangle: client-1 subscribe to a source, client 2 publishes it
 * 3) relay 1: client 3 subscribe to a source through relay, origin publishes it.
 * 4) relay 2: client 3 subscribe to a source through relay, client 2 publishes it.
 * 6) Realy 3: client 3 subscribe to a source through relay, client 4 publishes it.
 * 5) Relay 4: client 1 subscribe a source, client 4 publishes through relay.
 * 6) Multiple: client 1 subscribe to patter, origin, client[2] and client[4] publish.
 *
 * Configuration:
 * 
 *     origin[0]--+---- Client[1]
 *                |
 *                +---- Client[2]
 *                |
 *                +---- Relay[5]-+--- Client[3]
 *                               |
 *                               +--- Client[4]
 * 
 */

typedef struct st_quicrq_test_add_link_state_t {
    int nb_links;
    int nb_attachments;
} quicrq_test_add_link_state_t;

int quicrq_test_add_links(quicrq_test_config_t* config, quicrq_test_add_link_state_t* link_state, int node1, int node2) 
{
    int link1 = link_state->nb_links++;
    int link2 = link_state->nb_links++;
    int att1 = link_state->nb_attachments++;
    int att2 = link_state->nb_attachments++;
    if (link_state->nb_links > config->nb_links ||
        link_state->nb_attachments > config->nb_attachments) {
        return -1;
    }
    config->return_links[link1] = link2;
    config->attachments[att1].link_id = link1;
    config->attachments[att1].node_id = node1;
    config->return_links[link2] = link1;
    config->attachments[att2].link_id = link2;
    config->attachments[att2].node_id = node2;
    return 0;
}

/* Create a test network */
quicrq_test_config_t* quicrq_test_subscribe_config_create(uint64_t simulate_loss)
{
    /* Create a configuration with three nodes, four links, one source and 8 attachment points.*/
    quicrq_test_config_t* config = quicrq_test_config_create(6, 10, 10, 3);
    quicrq_test_add_link_state_t link_state = { 0 };
    if (config != NULL) {
        /* Create the contexts for the origin (0),  client-1 (1) and client-2 (2) */
        config->nodes[0] = quicrq_create(QUICRQ_ALPN,
            config->test_server_cert_file, config->test_server_key_file, NULL, NULL, NULL,
            config->ticket_encryption_key, sizeof(config->ticket_encryption_key),
            &config->simulated_time);
        config->nodes[5] = quicrq_create(QUICRQ_ALPN,
            config->test_server_cert_file, config->test_server_key_file, NULL, NULL, NULL,
            config->ticket_encryption_key, sizeof(config->ticket_encryption_key),
            &config->simulated_time);
        config->nodes[1] = quicrq_create(QUICRQ_ALPN,
            NULL, NULL, config->test_server_cert_store_file, NULL, NULL,
            NULL, 0, &config->simulated_time);
        config->nodes[2] = quicrq_create(QUICRQ_ALPN,
            NULL, NULL, config->test_server_cert_store_file, NULL, NULL,
            NULL, 0, &config->simulated_time);
        config->nodes[3] = quicrq_create(QUICRQ_ALPN,
            NULL, NULL, config->test_server_cert_store_file, NULL, NULL,
            NULL, 0, &config->simulated_time);
        config->nodes[4] = quicrq_create(QUICRQ_ALPN,
            NULL, NULL, config->test_server_cert_store_file, NULL, NULL,
            NULL, 0, &config->simulated_time);
        if (config->nodes[0] == NULL || config->nodes[1] == NULL || config->nodes[1] == NULL) {
            quicrq_test_config_delete(config);
            config = NULL;
        }
    }
    if (config != NULL) {
        /* Populate the attachments */
        if (quicrq_test_add_links(config, &link_state, 0, 1) != 0 ||
            quicrq_test_add_links(config, &link_state, 0, 2) != 0 ||
            quicrq_test_add_links(config, &link_state, 0, 5) != 0 ||
            quicrq_test_add_links(config, &link_state, 5, 3) != 0 ||
            quicrq_test_add_links(config, &link_state, 5, 4) != 0 ||
            link_state.nb_links != config->nb_links ||
            link_state.nb_attachments != config->nb_attachments) {
            quicrq_test_config_delete(config);
            config = NULL;
        }
    }
    if (config != NULL) {

        /* Set the desired loss pattern */
        config->simulate_loss = simulate_loss;
    }
    return config;
}

/* Subscribe test notification function
 * The function remembers the notified URL in the result context,
 * and also automatically subscribes to the notified url if
 * a connection context is specified.
 */
#define QUICRQ_SUBSCRIBE_TEST_RESULT_MAX 5
typedef struct st_quicrq_subscribe_test_result_t {
    quicrq_cnx_ctx_t* cnx_ctx;
    int use_datagrams;
    int nb_results;
    const char* result_file_name;
    const char* result_log_name;
    uint8_t* url[QUICRQ_SUBSCRIBE_TEST_RESULT_MAX];
    size_t url_length[QUICRQ_SUBSCRIBE_TEST_RESULT_MAX];
} quicrq_subscribe_test_result_t;

int quicrq_subscribe_test_notify(void* notify_ctx, const uint8_t* url, size_t url_length)
{
    int ret = 0;
    quicrq_subscribe_test_result_t* results = (quicrq_subscribe_test_result_t*)notify_ctx;

    if (results->nb_results >= QUICRQ_SUBSCRIBE_TEST_RESULT_MAX) {
        ret = -1;
    }
    else {
        results->url[results->nb_results] = (uint8_t*)malloc(url_length);
        if (results->url[results->nb_results] == NULL) {
            ret = -1;
        }
        else {
            results->url_length[results->nb_results] = url_length;
            results->nb_results++;
            /* Create a subscription to the test source on client */
            if (ret == 0 && results->cnx_ctx != NULL && results->nb_results == 1) {
                test_object_stream_ctx_t* object_stream_ctx = NULL;
                object_stream_ctx = test_object_stream_subscribe(results->cnx_ctx, url,
                    url_length, results->use_datagrams, results->result_file_name, results->result_log_name);
                if (object_stream_ctx == NULL) {
                    ret = -1;
                }
            }
        }
    }

    return ret;
}

/* Subscribe test */
int quicrq_subscribe_test_one(int is_real_time, int use_datagrams, uint64_t simulate_losses, int subscriber, int publisher, int pattern_length)
{
    int ret = 0;
    int nb_steps = 0;
    int nb_inactive = 0;
    int is_closed = 0;
    const uint64_t max_time = 360000000;
    const int max_inactive = 128;
    quicrq_test_config_t* config = quicrq_test_subscribe_config_create(simulate_losses);
    quicrq_cnx_ctx_t* cnx_ctx_subscriber = NULL;
    quicrq_cnx_ctx_t* cnx_ctx_2 = NULL;
    char media_source_path[512];
    char result_file_name[512];
    char result_log_name[512];
    char text_log_name[512];
    size_t nb_log_chars = 0;
    quicrq_stream_ctx_t* stream_ctx_subscriber = NULL;
    int stream_ctx_subscriber_is_closed = 0;
    uint64_t subscriber_close_time = UINT64_MAX;
    quicrq_subscribe_test_result_t results = { 0 };

    (void)picoquic_sprintf(text_log_name, sizeof(text_log_name), &nb_log_chars, "subscribe_textlog-%d-%d-%llx-%d-%d-%d.txt", is_real_time, use_datagrams,
        (unsigned long long)simulate_losses, subscriber, publisher, pattern_length);

    ret = test_media_derive_file_names((uint8_t*)QUICRQ_TEST_BASIC_SOURCE, strlen(QUICRQ_TEST_BASIC_SOURCE),
        use_datagrams, is_real_time, 0,
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
        ret = picoquic_set_textlog(config->nodes[0]->quic, text_log_name);
    }

    if (ret == 0) {
        /* Enable origin on node 0 */
        ret = quicrq_enable_origin(config->nodes[0], use_datagrams);
        if (ret != 0) {
            DBG_PRINTF("Cannot enable origin, ret = %d", ret);
        }
    }

    if (ret == 0) {
        /* Enable relay on node 5 */
        /* Configure the relay: joint client-server as default source and default consumer */
        /* Configure the relay: set the server address */
        struct sockaddr* addr_to = quicrq_test_find_send_addr(config, 5, 0);
        ret = quicrq_enable_relay(config->nodes[5], NULL, addr_to, use_datagrams);
        if (ret != 0) {
            DBG_PRINTF("Cannot enable relay, ret = %d", ret);
        }
    }

    if (ret == 0) {
        /* Add a test source to the configuration on publisher */
        config->object_sources[0] = test_media_object_source_publish(config->nodes[publisher], (uint8_t*)QUICRQ_TEST_BASIC_SOURCE,
            strlen(QUICRQ_TEST_BASIC_SOURCE), media_source_path, NULL, is_real_time, config->simulated_time);
        if (config->object_sources[0] == NULL) {
            ret = -1;
        }
    }

    if (ret == 0) {
        /* Create connection on subscriber */
        cnx_ctx_subscriber = quicrq_test_create_client_cnx(config, subscriber, (subscriber < 3)?0:5);
        if (cnx_ctx_subscriber == NULL) {
            ret = -1;
            DBG_PRINTF("Cannot create subscriber connection #1, ret = %d", ret);
        }
    }

    if (ret == 0 && publisher != 0) {
        /* Create a quicrq connection context on publisher */
        cnx_ctx_2 = quicrq_test_create_client_cnx(config, publisher, (publisher < 3) ? 0 : 5);
        if (cnx_ctx_2 == NULL) {
            ret = -1;
            DBG_PRINTF("Cannot create client connection #2, ret = %d", ret);
        }
    }

    if (ret == 0 && publisher != 0) {
        /* Start pushing from publisher */
        ret = quicrq_cnx_post_media(cnx_ctx_2, (uint8_t*)QUICRQ_TEST_BASIC_SOURCE, strlen(QUICRQ_TEST_BASIC_SOURCE), use_datagrams);
        if (ret != 0) {
            DBG_PRINTF("Cannot publish test media %s, ret = %d", QUICRQ_TEST_BASIC_SOURCE, ret);
        }
    }

    if (ret == 0) {
        /* Create a subscription to the test source on subscriber */
        results.cnx_ctx = cnx_ctx_subscriber;
        results.result_file_name = result_file_name;
        results.result_log_name = result_log_name;
        results.use_datagrams = use_datagrams;
        stream_ctx_subscriber = quicrq_cnx_subscribe_pattern(cnx_ctx_subscriber, (uint8_t*)QUICRQ_TEST_BASIC_SOURCE,
            pattern_length, quicrq_subscribe_test_notify, &results);

        if (stream_ctx_subscriber == 0) {
            ret = -1;
            DBG_PRINTF("Cannot subscribe to test pattern %s, l =%d, ret = %d", QUICRQ_TEST_BASIC_SOURCE, pattern_length, ret);
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
        /* Drop the subscribe pattern stream after 5 seconds */
        if (stream_ctx_subscriber != NULL && config->simulated_time >= 5000000 && !stream_ctx_subscriber_is_closed) {
            ret = quicrq_cnx_subscribe_pattern_close(cnx_ctx_subscriber, stream_ctx_subscriber);
            stream_ctx_subscriber_is_closed = 1;
        }
        /* if the media is sent and received, exit the loop */
        if (config->nodes[subscriber]->first_cnx == NULL &&
            (publisher == 0 ||
                config->nodes[publisher]->first_cnx == NULL)) {
            DBG_PRINTF("%s", "Exit loop after client connections closed.");
            break;
        }
        else {
            int publisher_stream_closed = 
                config->nodes[publisher]->first_cnx == NULL ||
                config->nodes[publisher]->first_cnx->first_stream == NULL;

            int subscriber_stream_closed = config->nodes[subscriber]->first_cnx == NULL ||
                config->nodes[subscriber]->first_cnx->first_stream == NULL;

            if (subscriber_stream_closed) {
                DBG_PRINTF("%s", "Done");
            }

            if (subscriber_stream_closed && subscriber_close_time > config->simulated_time) {
                subscriber_close_time = config->simulated_time;
            }

            if (!is_closed && subscriber_stream_closed && publisher_stream_closed) {
                /* Client is done. Close connections without waiting for timer -- if not closed yet */
                is_closed = 1;
                for (int c_nb = 1; ret == 0 && c_nb <= 5; c_nb++) {
                    if (config->nodes[c_nb]->first_cnx != NULL) {
                        ret = quicrq_close_cnx(config->nodes[c_nb]->first_cnx);
                        if (ret != 0) {
                            DBG_PRINTF("Cannot close client connection, ret = %d", ret);
                        }
                    }
                }
            }
        }
    }

    if (ret == 0 && (!is_closed || subscriber_close_time > 12000000)) {
        DBG_PRINTF("Session was not properly closed, time = %" PRIu64, subscriber_close_time);
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

int quicrq_subscribe_basic_test()
{
    int is_real_time = 1;
    int use_datagrams = 0;
    uint64_t simulate_losses = 0;
    int subscriber = 1;
    int publisher = 0;
    int pattern_length = (int)strlen(QUICRQ_TEST_BASIC_SOURCE);

    int ret = quicrq_subscribe_test_one(is_real_time,
        use_datagrams, simulate_losses,
        subscriber, publisher, pattern_length);

    return ret;
}

int quicrq_subscribe_client_test()
{
    int is_real_time = 1;
    int use_datagrams = 1;
    uint64_t simulate_losses = 0;
    int subscriber = 1;
    int publisher = 2;
    int pattern_length = (int)strlen(QUICRQ_TEST_BASIC_SOURCE);

    int ret = quicrq_subscribe_test_one(is_real_time,
        use_datagrams, simulate_losses,
        subscriber, publisher, pattern_length);

    return ret;
}

int quicrq_subscribe_datagram_test()
{
    int is_real_time = 1;
    int use_datagrams = 1;
    uint64_t simulate_losses = 0;
    int subscriber = 1;
    int publisher = 0;
    int pattern_length = (int)strlen(QUICRQ_TEST_BASIC_SOURCE) - 5;

    int ret = quicrq_subscribe_test_one(is_real_time,
        use_datagrams, simulate_losses,
        subscriber, publisher, pattern_length);

    return ret;
}

int quicrq_subscribe_relay1_test()
{
    int is_real_time = 1;
    int use_datagrams = 1;
    uint64_t simulate_losses = 0;
    int subscriber = 1;
    int publisher = 3;
    int pattern_length = (int)strlen(QUICRQ_TEST_BASIC_SOURCE);

    int ret = quicrq_subscribe_test_one(is_real_time,
        use_datagrams, simulate_losses,
        subscriber, publisher, pattern_length);

    return ret;
}

int quicrq_subscribe_relay2_test()
{
    int is_real_time = 1;
    int use_datagrams = 1;
    uint64_t simulate_losses = 0;
    int subscriber = 3;
    int publisher = 2;
    int pattern_length = (int)strlen(QUICRQ_TEST_BASIC_SOURCE);

    int ret = quicrq_subscribe_test_one(is_real_time,
        use_datagrams, simulate_losses,
        subscriber, publisher, pattern_length);

    return ret;
}

int quicrq_subscribe_relay3_test()
{
    int is_real_time = 1;
    int use_datagrams = 1;
    uint64_t simulate_losses = 0;
    int subscriber = 3;
    int publisher = 4;
    int pattern_length = (int)strlen(QUICRQ_TEST_BASIC_SOURCE);

    int ret = quicrq_subscribe_test_one(is_real_time,
        use_datagrams, simulate_losses,
        subscriber, publisher, pattern_length);

    return ret;
}