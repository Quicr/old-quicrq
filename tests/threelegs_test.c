#include <string.h>
#include "picoquic_set_textlog.h"
#include "picoquic_set_binlog.h"
#include "quicrq.h"
#include "quicrq_relay.h"
#include "quicrq_internal.h"
#include "quicrq_test_internal.h"

/* Three legs test:
 * One publishing client attached directly to the server,
 * Two other clients attached through a relay.
 * The configuration diagram is:
 * 
 *           S
 *          / \
 *         R   C1
 *        / \
 *       C2  C3
 *      
 */

/* Create a test network */
quicrq_test_config_t* quicrq_test_threelegs_config_create(uint64_t simulate_loss)
{
    /* Create a configuration with five nodes, eight links, one source and 5 attachment points.*/
    quicrq_test_config_t* config = quicrq_test_config_create(5, 8, 8, 1);
    if (config != NULL) {
        /* Create the contexts for the origin (0),  relay (1) and client-1 (2), client=2 (3) and client-3(4) */
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
        config->nodes[4] = quicrq_create(QUICRQ_ALPN,
            NULL, NULL, config->test_server_cert_store_file, NULL, NULL,
            NULL, 0, &config->simulated_time);
        config->sources[0].srce_ctx = NULL;
        if (config->nodes[0] == NULL || config->nodes[1] == NULL || config->nodes[1] == NULL) {
            quicrq_test_config_delete(config);
            config = NULL;
        }
    }
    if (config != NULL) {
        /* Populate the links and attachments */
        /* S to R: links 0 and 1 */
        config->return_links[0] = 1;
        config->attachments[0].link_id = 0;
        config->attachments[0].node_id = 0;
        config->return_links[1] = 0;
        config->attachments[1].link_id = 1;
        config->attachments[1].node_id = 1;
        /* S to C1: links 2 and 3 */
        config->return_links[2] = 3;
        config->attachments[2].link_id = 2;
        config->attachments[2].node_id = 0;
        config->return_links[3] = 2;
        config->attachments[3].link_id = 3;
        config->attachments[3].node_id = 2;
        /* R to C2: links 4 and 5 */
        config->return_links[4] = 5;
        config->attachments[4].link_id = 4;
        config->attachments[4].node_id = 1;
        config->return_links[5] = 4;
        config->attachments[5].link_id = 5;
        config->attachments[5].node_id = 3;
        /* R to C3: links 6 and 7 */
        config->return_links[6] = 7;
        config->attachments[6].link_id = 6;
        config->attachments[6].node_id = 1;
        config->return_links[7] = 6;
        config->attachments[7].link_id = 7;
        config->attachments[7].node_id = 4;
        /* Set the desired loss pattern */
        config->simulate_loss = simulate_loss;
    }
    return config;
}

/* Basic relay test */
int quicrq_threelegs_test_one(int use_datagrams, uint64_t simulate_losses)
{
    int ret = 0;
    int nb_steps = 0;
    int nb_inactive = 0;
    int is_closed = 0;
    const uint64_t max_time = 360000000;
    const int max_inactive = 128;
    quicrq_test_config_t* config = quicrq_test_threelegs_config_create(simulate_losses);
    char media_source_path[512];
    char result_file_name_1[256];
    char result_file_name_2[256];
    char result_log_name_1[256];
    char result_log_name_2[256];
    char* result_file_name[2] = { result_file_name_1, result_file_name_2 };
    char* result_log_name[2] = { result_log_name_1, result_log_name_2 };

    char text_log_name[512];
    size_t nb_log_chars = 0;
    uint64_t start_delay[3] = { 1000000, 0, 2000000 };
    int client_is_started[3] = { 0, 0, 0 };
    quicrq_cnx_ctx_t* cnx_ctx[3] = { NULL, NULL, NULL };
    int partial_closure = 0;


    (void)picoquic_sprintf(text_log_name, sizeof(text_log_name), &nb_log_chars, "threelegs_textlog-%d-%llx.txt", use_datagrams, (unsigned long long)simulate_losses);
    for (int i = 0; i < 2; i++) {
        (void)picoquic_sprintf(result_file_name[i], sizeof(result_file_name_1), &nb_log_chars, "threelegs-video1-recv-%d-%d-%llx.bin",
            i+1, use_datagrams, (unsigned long long)simulate_losses);
        (void)picoquic_sprintf(result_log_name[i], sizeof(result_log_name_1), &nb_log_chars, "threelegs-video1-log-%d-%d-%llx.csv",
            i+1, use_datagrams, (unsigned long long)simulate_losses);
    }

    if (config == NULL) {
        ret = -1;
    }

    /* Locate the source and reference file */
    if (picoquic_get_input_path(media_source_path, sizeof(media_source_path),
        quicrq_test_solution_dir, QUICRQ_TEST_BASIC_SOURCE) != 0) {
        ret = -1;
    }

    /* Add QUIC level log to the relay */
    if (ret == 0) {
        ret = picoquic_set_textlog(config->nodes[1]->quic, text_log_name);
    }

    if (ret == 0) {
        /* Enable origin on node 0 */
        ret = quicrq_enable_origin(config->nodes[0], use_datagrams);
        if (ret != 0) {
            DBG_PRINTF("Cannot enable origin, ret = %d", ret);
        }
    }

    if (ret == 0) {
        /* test source is always client # 1 (node #2) */
        int publish_node = 2;

        config->sources[0].srce_ctx = test_media_publish(config->nodes[publish_node], (uint8_t*)QUICRQ_TEST_BASIC_SOURCE, strlen(QUICRQ_TEST_BASIC_SOURCE),
            media_source_path, NULL, 1, &config->sources[0].next_source_time, 0);
        if (config->sources[0].srce_ctx == NULL) {
            ret = -1;
            DBG_PRINTF("Cannot publish test media %s, ret = %d", QUICRQ_TEST_BASIC_SOURCE, ret);
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

    while (ret == 0 && nb_inactive < max_inactive && config->simulated_time < max_time) {
        /* Run the simulation. Monitor the connection. Monitor the media. */
        int is_active = 0;
        uint64_t app_wake_time = UINT64_MAX;
        for (int i = 0; ret == 0 && i < 3; i++) {
            if (!client_is_started[i]) {
                if (config->simulated_time >= start_delay[i]) {
                    /* Start client i */
                    int c_node_id = i + 2;
                    int target_id = (i == 0) ? 0 : 1;

                    if (ret == 0) {
                        /* Create a quicrq connection context on client */
                        cnx_ctx[i] = quicrq_test_create_client_cnx(config, c_node_id, target_id);
                        if (cnx_ctx[i] == NULL) {
                            ret = -1;
                            DBG_PRINTF("Cannot create client connection %d, ret = %d", c_node_id, ret);
                        }
                    }
                    if (ret == 0) {
                        if (i == 0) {
                            /* Start pushing from client 1 */
                            ret = quicrq_cnx_post_media(cnx_ctx[i], (uint8_t*)QUICRQ_TEST_BASIC_SOURCE, strlen(QUICRQ_TEST_BASIC_SOURCE), use_datagrams);
                            if (ret != 0) {
                                DBG_PRINTF("Cannot publish test media %s, ret = %d", QUICRQ_TEST_BASIC_SOURCE, ret);
                            }
                        }
                        else {
                            /* Create a subscription to the test source on other client*/
                            ret = test_media_subscribe(cnx_ctx[i], (uint8_t*)QUICRQ_TEST_BASIC_SOURCE, strlen(QUICRQ_TEST_BASIC_SOURCE),
                                use_datagrams, result_file_name[i - 1], result_log_name[i - 1]);
                            if (ret != 0) {
                                DBG_PRINTF("Cannot subscribe to test media %s, ret = %d", QUICRQ_TEST_BASIC_SOURCE, ret);
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

        ret = quicrq_test_loop_step_ex(config, &is_active, app_wake_time);
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
            int is_client_closed[3] = { 0, 0, 0 };
            int is_client_done[3] = { 0, 0, 0 };

            for (int i = 0; i < 3; i++) {
                int node_id = i + 2;
                is_client_closed[i] = client_is_started[i] && config->nodes[node_id]->first_cnx == NULL;
                is_client_done[i] = client_is_started[i] && (config->nodes[node_id]->first_cnx == NULL || config->nodes[node_id]->first_cnx->first_stream == NULL);
                all_closed &= is_client_closed[i];
                all_done &= is_client_done[i];
                nb_done += is_client_done[i];
            }

            if (all_closed) {
                DBG_PRINTF("%s", "Exit loop after all client connection closed.");
                break;
            }
            else if (!is_closed) {
                if (all_done) {
                    /* Clients are done. Close connections without waiting for timer -- if not closed yet */
                    is_closed = 1;
                    for (int c_nb = 2; ret == 0 && c_nb < 5; c_nb++) {
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
                        DBG_PRINTF("Partial closure: client 1 (%d), client 2 (%d), client 3 (%d), time = %" PRIu64,
                            is_client_done[0], is_client_done[1], is_client_done[2], config->simulated_time);
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
        for (int i = 0; ret == 0 && i < 2; i++) {
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

int quicrq_threelegs_basic_test()
{
    int ret = quicrq_threelegs_test_one(0, 0);

    return ret;
}

int quicrq_threelegs_datagram_test()
{
    int ret = quicrq_threelegs_test_one(1, 0);

    return ret;
}

int quicrq_threelegs_datagram_loss_test()
{
    int ret = quicrq_threelegs_test_one(1, 0x37880);

    return ret;
}
