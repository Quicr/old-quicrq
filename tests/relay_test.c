#include <string.h>
#include <stdlib.h>
#include "picoquic_set_textlog.h"
#include "picoquic_set_binlog.h"
#include "quicrq.h"
#include "quicrq_relay.h"
#include "quicrq_internal.h"
#include "quicrq_relay_internal.h"
#include "quicrq_test_internal.h"

/* Create a test network */
quicrq_test_config_t* quicrq_test_relay_config_create(uint64_t simulate_loss)
{
    /* Create a configuration with three nodes, four links, one source and 8 attachment points.*/
    quicrq_test_config_t* config = quicrq_test_config_create(3, 4, 4, 1);
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
        config->sources[0].srce_ctx = NULL;
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
        config->attachments[2].node_id = 1;
        config->return_links[3] = 2;
        config->attachments[3].link_id = 3;
        config->attachments[3].node_id = 2;
        /* Set the desired loss pattern */
        config->simulate_loss = simulate_loss;
    }
    return config;
}

/* Basic relay test */
int quicrq_relay_test_one(int is_real_time, int use_datagrams, uint64_t simulate_losses, int is_from_client)
{
    int ret = 0;
    int nb_steps = 0;
    int nb_inactive = 0;
    int is_closed = 0;
    const uint64_t max_time = 360000000;
    const int max_inactive = 128;
    quicrq_test_config_t* config = quicrq_test_relay_config_create(simulate_losses);
    quicrq_cnx_ctx_t* cnx_ctx = NULL;
    char media_source_path[512];
    char result_file_name[512];
    char result_log_name[512];
    char text_log_name[512];
    size_t nb_log_chars = 0;

    (void)picoquic_sprintf(text_log_name, sizeof(text_log_name), &nb_log_chars, "relay_textlog-%d-%d-%d-%llx.txt", is_real_time, use_datagrams, is_from_client, (unsigned long long)simulate_losses);
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
        int publish_node = (is_from_client) ? 2 : 0;

        config->sources[0].srce_ctx = test_media_publish(config->nodes[publish_node], (uint8_t*)QUICRQ_TEST_BASIC_SOURCE, strlen(QUICRQ_TEST_BASIC_SOURCE),
            media_source_path, NULL, is_real_time, &config->sources[0].next_source_time, 0);
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

    if (ret == 0) {
        /* Create a quirq connection context on client */
        cnx_ctx = quicrq_test_create_client_cnx(config, 2, 1);
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
        if (config->nodes[2]->first_cnx == NULL) {
            DBG_PRINTF("%s", "Exit loop after client connection closed.");
            break;
        }
        else {
            /* TODO: add closing condition on server */
            int client_stream_closed = config->nodes[2]->first_cnx->first_stream == NULL;
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

int quicrq_relay_basic_test()
{
    int ret = quicrq_relay_test_one(1, 0, 0, 0);

    return ret;
}

int quicrq_relay_datagram_test()
{
    int ret = quicrq_relay_test_one(1, 1, 0, 0);

    return ret;
}

int quicrq_relay_datagram_loss_test()
{
    int ret = quicrq_relay_test_one(1, 1, 0x7080, 0);

    return ret;
}

int quicrq_relay_basic_client_test()
{
    int ret = quicrq_relay_test_one(1, 0, 0, 1);

    return ret;
}

int quicrq_relay_datagram_client_test()
{
    int ret = quicrq_relay_test_one(1, 1, 0, 1);

    return ret;
}

/* Unit tests of the relay specific cache
 */
#define RELAY_TEST_OBJECT_MAX 32
typedef struct st_relay_test_object_t {
    size_t length;
    uint8_t data[RELAY_TEST_OBJECT_MAX];
} relay_test_object_t;

relay_test_object_t relay_test_objects[] = {
    { 25, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24}},
    { 8, { 10, 11, 12, 13, 14, 15, 16, 17 }},
    { 9, { 20, 21, 22, 23, 24, 25, 26, 27, 28 }},
    { 9, { 30, 31, 32, 33, 34, 35, 36, 37, 38 }},
    { 10, { 40, 41, 42, 43, 44, 45, 46, 47, 48, 49 }},
    { 5, { 50, 51, 52, 53, 54 }},
    { 6, { 60, 61, 62, 63, 64, 65 }},
    { 7, { 70, 71, 72, 73, 74, 75, 76 }},
    { 30, { 80, 81, 82, 83, 84, 85, 86, 87, 88, 89,
            90, 91, 92, 93, 94, 95, 96, 97, 98, 99,
            100, 101, 102, 103, 104, 105, 106, 107, 108, 109 }}
};

size_t nb_relay_test_objects = sizeof(relay_test_objects) / sizeof(relay_test_object_t);

int quicrq_relay_cache_verify(quicrq_relay_cached_media_t* cached_ctx)
{
    int ret = 0;
    size_t nb_fragments_found = 0;
    for (size_t f_id = 0; ret == 0 && f_id < nb_relay_test_objects; f_id++) {
        size_t offset = 0;
        while (ret == 0 && offset < relay_test_objects[f_id].length) {
            /* Get fragment at specified offset */
            quicrq_relay_cached_fragment_t* fragment = quicrq_relay_cache_get_fragment(cached_ctx, f_id, offset);
            if (fragment == NULL) {
                DBG_PRINTF("Cannot find fragment, object %zu, offset %zu", f_id, offset);
                ret = -1;
            }
            /* Check that length does not overflow */
            if (ret == 0 && offset + fragment->data_length > relay_test_objects[f_id].length) {
                DBG_PRINTF("Fragment overflow, object %zu, offset %zu, length %zu", f_id, offset, fragment->data_length);
                ret = -1;
            }
            /* Verify data matches */
            if (ret == 0 && memcmp(relay_test_objects[f_id].data + offset, fragment->data, fragment->data_length) != 0) {
                DBG_PRINTF("Fragment data incorrect, object %zu, offset %zu, length %zu", f_id, offset, fragment->data_length);
                ret = -1;
            }
            /* Verify is_last_fragment */
            if (ret == 0) {
                int should_be_last = (offset + fragment->data_length) >= relay_test_objects[f_id].length;
                if (should_be_last && !fragment->is_last_fragment) {
                    DBG_PRINTF("Fragment should be last, object %zu, offset %zu, length %zu", f_id, offset, fragment->data_length);
                    ret = -1;
                }
                else if (!should_be_last && fragment->is_last_fragment) {
                    DBG_PRINTF("Fragment should not be last, object %zu, offset %zu, length %zu", f_id, offset, fragment->data_length);
                    ret = -1;
                }
            }
            /* Update offset, increment fragment count */
            if (ret == 0) {
                offset += fragment->data_length;
                nb_fragments_found += 1;
            }
        }
    }
    if (ret == 0) {
        /* Check that cache contains exactly the expected number of fragments */
        if (cached_ctx->fragment_tree.size != nb_fragments_found) {
            DBG_PRINTF("Found %zu fragments, cache contains %zu", nb_fragments_found, cached_ctx->fragment_tree.size);
            ret = -1;
        }
    }
    if (ret == 0) {
        /* Verify the chain of fragments */
        quicrq_relay_cached_fragment_t* fragment = cached_ctx->first_fragment;
        quicrq_relay_cached_fragment_t* previous_fragment = NULL;
        size_t nb_in_chain = 0;
        while (fragment != NULL) {
            nb_in_chain++;
            previous_fragment = fragment;
            fragment = fragment->next_in_order;
        }
        if (nb_in_chain != cached_ctx->fragment_tree.size) {
            DBG_PRINTF("Found %zu fragments in chain, cache contains %zu", nb_in_chain, cached_ctx->fragment_tree.size);
            ret = -1;
        }
        else if (previous_fragment != cached_ctx->last_fragment) {
            DBG_PRINTF("%s", "Last in chain does not match last fragment");
            ret = -1;
        }
    }
    if (ret == 0) {
        /* verify that the number of objects received matches the expected count */
        if (cached_ctx->nb_object_received != nb_relay_test_objects) {
            DBG_PRINTF("Received %zu objects instead of %zu", cached_ctx->nb_object_received, nb_relay_test_objects - 1);
            ret = -1;
        }
    }
    return ret;
}

int quicrq_relay_cache_fill_test_one(size_t fragment_max, size_t start_object, size_t skip, int nb_pass)
{
    int ret = 0;
    int nb_skipped = 0;
    /* Create a cache */
    quicrq_media_source_ctx_t* srce_ctx = (quicrq_media_source_ctx_t*)malloc(sizeof(quicrq_media_source_ctx_t));
    quicrq_relay_cached_media_t* cached_ctx = quicrq_relay_create_cache_ctx();

    if (cached_ctx == NULL || srce_ctx == NULL) {
        ret = -1;
    }
    else {
        memset(srce_ctx, 0, sizeof(quicrq_media_source_ctx_t));
        cached_ctx->srce_ctx = srce_ctx;
        /* send a first set of segments,
         * starting with designated object */
        for (int pass = 1; ret == 0 && pass <= nb_pass; pass++) {
            size_t skip_count = 0;
            for (size_t f_id = 0; ret == 0 && f_id < nb_relay_test_objects; f_id++) {
                size_t offset = 0;
                while (ret == 0 && offset < relay_test_objects[f_id].length) {
                    size_t data_length = relay_test_objects[f_id].length - offset;
                    int is_last_fragment = 1;
                    int should_skip = 0;
                    if (data_length > fragment_max) {
                        data_length = fragment_max;
                        is_last_fragment = 0;
                    }
                    /* If we are skipping some objects:
                     *    - these objects are skipped in the 1st pass
                     *    - only these objects are skipped in the last pass
                     *    - non-skipped objects are repeated as duplicate in intermediate objects.
                     */
                    if (f_id < start_object) {
                        if (pass < nb_pass) {
                            should_skip = 1;
                        }
                        else {
                            should_skip = 0;
                        }
                    }
                    else if (skip != 0) {
                        skip_count++;
                        if (skip_count >= skip) {
                            if (pass < nb_pass) {
                                should_skip = 0;
                            }
                            else {
                                should_skip = 1;
                            }
                            skip_count = 0;
                        }
                        else {
                            if (pass < nb_pass) {
                                should_skip = 1;
                            }
                            else {
                                should_skip = 0;
                            }
                        }
                    }
                    if (!should_skip) {
                        ret = quicrq_relay_propose_fragment_to_cache(cached_ctx, 
                            relay_test_objects[f_id].data + offset, f_id, offset, is_last_fragment, data_length);
                        if (ret != 0) {
                            DBG_PRINTF("Proposed segment fails, object %zu, offset %zu, pass %d, ret %d", f_id, offset, pass, ret);
                        }
                    }
                    else {
                        nb_skipped++;
                    }
                    offset += data_length;
                }
            }
        }
        if (skip != 0 && nb_skipped == 0) {
            DBG_PRINTF("Expected skip %zu, nothing skipped", skip);
            ret = -1;
        }
         /* Verify the cache is as expected */
        if (ret == 0) {
            ret = quicrq_relay_cache_verify(cached_ctx);
        }
    }

    if (srce_ctx != NULL) {
        free(srce_ctx);
    }

    if (cached_ctx != NULL) {
        /* Delete the cache */
        quicrq_relay_delete_cache_ctx(cached_ctx);
    }

    return ret;
}

/* For the purpose of simulating the picoquic API, we copy here
 * the definition of the context used by the API
 * `picoquic_provide_datagram_buffer` */
typedef struct st_relay_test_datagram_buffer_argument_t {
    uint8_t* bytes0; /* Points to the beginning of the encoding of the datagram object */
    uint8_t* bytes; /* Position after encoding the datagram object type */
    uint8_t* bytes_max; /* Pointer to the end of the packet */
    uint8_t* after_data; /* Pointer to end of data written by app */
    size_t allowed_space; /* Data size from bytes to end of packet */
} relay_test_datagram_buffer_argument_t;

/* Simulate a relay trying to forward data after it is added to the cache. */
int quicr_relay_cache_publish_simulate(quicrq_relay_publisher_context_t* pub_ctx, quicrq_relay_cached_media_t* cached_ctx_p, 
    uint64_t *sequential_object_id, size_t *sequential_offset,
    int is_datagram, uint64_t current_time)
{
    int ret = 0;
    uint8_t data[1024];
    int is_last_fragment;
    int is_media_finished;
    int is_still_active;
    uint64_t object_id;
    size_t fragment_offset = 0;
    uint8_t* fragment = NULL;
    size_t fragment_length;

    do {
        fragment_length = 0;
        if (is_datagram) {
            int at_least_one_active = 0;
            int media_was_sent = 0;
            int not_ready = 0;

            /* Setup a datagram buffer conext to mimic picoquic's behavior */
            relay_test_datagram_buffer_argument_t d_context = { 0 };
            data[0] = 0x30;
            d_context.bytes0 = &data[0];
            d_context.bytes = &data[1];
            d_context.after_data = &data[0];
            d_context.bytes_max = &data[0] + 1024;
            d_context.allowed_space = 1023;
            /* Call the prepare function */
            ret = quicrq_relay_datagram_publisher_prepare(pub_ctx, 0, &d_context, d_context.allowed_space,
                &media_was_sent, &at_least_one_active, &not_ready);
            /* Decode the datagram header to find the coded_fragment */
            if (ret == 0 && d_context.after_data > d_context.bytes0) {
                const uint8_t* bytes = d_context.bytes0;
                size_t datagram_length;
                /* skip the padding */
                while (*bytes == 0 && bytes < d_context.bytes_max) {
                    bytes++;
                }
                if (*bytes == 0x30) {
                    bytes++;
                    datagram_length = d_context.bytes_max - bytes;
                }
                else if (*bytes == 0x31) {
                    /* decode the length */
                    bytes = picoquic_frames_varlen_decode(bytes, d_context.bytes_max, &datagram_length);
                    if (bytes == NULL || datagram_length < 1) {
                        ret = -1;
                    }
                }
                else {
                    ret = 1;
                }
                if (ret == 0) {
                    /* decode the datagram header */
                    uint64_t datagram_stream_id;
                    uint64_t object_offset;
                    const uint8_t* datagram_max = bytes + datagram_length;

                    bytes = quicrq_datagram_header_decode(bytes, datagram_max, &datagram_stream_id,
                        &object_id, &object_offset, &is_last_fragment);
                    if (bytes == NULL) {
                        DBG_PRINTF("Cannot decode datagram header, length = %zu", datagram_length);
                        ret = -1;
                    }
                    else if (datagram_stream_id != 0) {
                        DBG_PRINTF("Unexpected datagram stream id: %" PRIu64, datagram_stream_id);
                        ret = -1;
                    } else {
                        fragment = (uint8_t *)bytes;
                        fragment_length = datagram_max - bytes;
                        fragment_offset = (size_t)object_offset;
                    }
                }
            }
        }
        else {
            /* The first call to the publisher functions positions to the current objectid, offset, etc. */
            ret = quicrq_relay_publisher_fn(quicrq_media_source_get_data, pub_ctx, NULL, 1024, &fragment_length,
                &is_last_fragment, &is_media_finished, &is_still_active, current_time);
            if (fragment_length > 0) {
                object_id = pub_ctx->current_object_id;
                fragment_offset = pub_ctx->current_offset;
                if (object_id != *sequential_object_id) {
                    DBG_PRINTF("Expected object id = %" PRIu64 ", got %" PRIu64, *sequential_object_id, object_id);
                    ret = -1;
                }
                if (fragment_offset != *sequential_offset) {
                    DBG_PRINTF("For object id = %" PRIu64 ", expected offset %zu got %zu", object_id, *sequential_offset, fragment_offset);
                    ret = -1;
                }
                /* The second call to the media function copies the data at the required space. */
                ret = quicrq_relay_publisher_fn(quicrq_media_source_get_data, pub_ctx, data, 1024, &fragment_length,
                    &is_last_fragment, &is_media_finished, &is_still_active, current_time);
                fragment = data;
                if (is_last_fragment) {
                    *sequential_object_id += 1;
                    *sequential_offset = 0;
                }
                else {
                    *sequential_offset += fragment_length;
                    if (*sequential_offset > RELAY_TEST_OBJECT_MAX) {
                        DBG_PRINTF("Wrong offset: %zu", *sequential_offset);
                        ret = -1;
                    }
                }
            }
        }
        if (ret == 0 && fragment_length > 0) {
            /* submit to the media cache */
            ret = quicrq_relay_propose_fragment_to_cache(cached_ctx_p,
                fragment, object_id, fragment_offset, is_last_fragment, fragment_length);
        }
    } while (ret == 0 && fragment_length > 0);

    return ret;
}

int quicrq_relay_cache_publish_test_one(int is_datagram)
{
    int ret = 0;
    int nb_skipped = 0;
    /* Create caches and contexts */
    uint64_t current_time = 0;
    uint64_t sequential_object_id = 0;
    size_t sequential_offset = 0;
    quicrq_media_source_ctx_t* srce_ctx = (quicrq_media_source_ctx_t*)malloc(sizeof(quicrq_media_source_ctx_t));
    quicrq_relay_cached_media_t* cached_ctx = quicrq_relay_create_cache_ctx();
    quicrq_media_source_ctx_t* srce_ctx_p = (quicrq_media_source_ctx_t*)malloc(sizeof(quicrq_media_source_ctx_t));
    quicrq_relay_cached_media_t* cached_ctx_p = quicrq_relay_create_cache_ctx();
    quicrq_relay_publisher_context_t* pub_ctx = (quicrq_relay_publisher_context_t*)malloc(sizeof(quicrq_relay_publisher_context_t));
    if (cached_ctx == NULL || srce_ctx == NULL || cached_ctx_p == NULL || srce_ctx_p == NULL || pub_ctx == NULL) {
        ret = -1;
    }
    else {
        memset(srce_ctx, 0, sizeof(quicrq_media_source_ctx_t));
        cached_ctx->srce_ctx = srce_ctx;
        memset(srce_ctx_p, 0, sizeof(quicrq_media_source_ctx_t));
        cached_ctx_p->srce_ctx = srce_ctx_p;
        memset(pub_ctx, 0, sizeof(quicrq_relay_publisher_context_t));
        pub_ctx->cache_ctx = cached_ctx;
    }

    /* send a first set of segments,
     * starting with designated object */
    for (int pass = 1; ret == 0 && pass <= 2; pass++) {
        size_t skip_count = 0;
        for (size_t f_id = 0; ret == 0 && f_id < nb_relay_test_objects; f_id++) {
            size_t offset = 0;
            while (ret == 0 && offset < relay_test_objects[f_id].length) {
                size_t data_length = relay_test_objects[f_id].length - offset;
                int is_last_fragment = 1;
                int should_skip = 0;
                if (data_length > 8) {
                    data_length = 8;
                    is_last_fragment = 0;
                }
                skip_count++;
                if (skip_count >= 2) {
                    if (pass < 2) {
                        should_skip = 1;
                    }
                    else {
                        should_skip = 0;
                    }
                    skip_count = 0;
                }
                else {
                    if (pass < 2) {
                        should_skip = 0;
                    }
                    else {
                        should_skip = 1;
                    }
                }
                if (!should_skip) {
                    ret = quicrq_relay_propose_fragment_to_cache(cached_ctx,
                        relay_test_objects[f_id].data + offset, f_id, offset, is_last_fragment, data_length);
                    if (ret != 0) {
                        DBG_PRINTF("Proposed segment fails, object %zu, offset %zu, pass %d, ret %d", f_id, offset, pass, ret);
                    }
                    /* Simulate waking up the consumer and polling the data */
                    ret = quicr_relay_cache_publish_simulate(pub_ctx, cached_ctx_p, &sequential_object_id, &sequential_offset,
                        is_datagram, current_time);
                }
                else {
                    nb_skipped++;
                }
                offset += data_length;
                current_time += 1000;
            }
        }
    }
    /* verify that the relay cache has the expected content */
    if (ret == 0) {
        ret = quicrq_relay_cache_verify(cached_ctx);
    }

    /* Verify that the consumer cache has the expected content */
    if (ret == 0) {
        ret = quicrq_relay_cache_verify(cached_ctx_p);
    }

    if (srce_ctx != NULL) {
        free(srce_ctx);
    }

    if (cached_ctx != NULL) {
        /* Delete the cache */
        quicrq_relay_delete_cache_ctx(cached_ctx);
    }

    if (srce_ctx_p != NULL) {
        free(srce_ctx_p);
    }

    if (cached_ctx_p != NULL) {
        /* Delete the cache */
        quicrq_relay_delete_cache_ctx(cached_ctx_p);
    }

    if (pub_ctx != NULL) {
        free(pub_ctx);
    }

    return ret;
}


int quicrq_relay_cache_fill_test()
{
    int ret = 0;
    /* basic test, single pass, nothing skipped, entire objects. */
    if (ret == 0) {
        ret = quicrq_relay_cache_fill_test_one(100, 0, 0, 1);
        if (ret != 0) {
            DBG_PRINTF("Basic test returns %d", ret);
        }
    }
    /* fragment test, single pass, nothing skipped. */
    if (ret == 0) {
        ret = quicrq_relay_cache_fill_test_one(8, 0, 0, 1);
        if (ret != 0) {
            DBG_PRINTF("Fragment test returns %d", ret);
        }
    }
    /* fragment test, two passes, skip even. */
    if (ret == 0) {
        ret = quicrq_relay_cache_fill_test_one(8, 0, 2, 2);
        if (ret != 0) {
            DBG_PRINTF("Skip even test returns %d", ret);
        }
    }
    /* fragment test, two passes, skip odd. */
    if (ret == 0) {
        ret = quicrq_relay_cache_fill_test_one(8, 0, 1, 2);
        if (ret != 0) {
            DBG_PRINTF("Skip odd test returns %d", ret);
        }
    }
    /* fragment test, three passes, skip odd. */
    if (ret == 0) {
        ret = quicrq_relay_cache_fill_test_one(8, 0, 1, 3);
        if (ret != 0) {
            DBG_PRINTF("Three passes test returns %d", ret);
        }
    }
    /* Receive test, stream. */
    if (ret == 0) {
        ret = quicrq_relay_cache_publish_test_one(0);
        if (ret != 0) {
            DBG_PRINTF("Cached stream relay test returns %d", ret);
        }
    }
    /* Receive test, datagram. */
    if (ret == 0) {
        ret = quicrq_relay_cache_publish_test_one(0);
        if (ret != 0) {
            DBG_PRINTF("Cached datagram relay test returns %d", ret);
        }
    }




    return ret;
}