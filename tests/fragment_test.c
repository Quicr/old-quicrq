#include <string.h>
#include <stdlib.h>
#include "picoquic_set_textlog.h"
#include "picoquic_set_binlog.h"
#include "quicrq.h"
#include "quicrq_relay.h"
#include "quicrq_internal.h"
#include "quicrq_fragment.h"
#include "quicrq_test_internal.h"

/* Unit tests of the fragment cache
 */
#define RELAY_TEST_OBJECT_MAX 32
typedef struct st_fragment_test_object_t {
    uint64_t group_id;
    uint64_t object_id;
    size_t length;
    uint8_t data[RELAY_TEST_OBJECT_MAX];
} fragment_test_object_t;

fragment_test_object_t fragment_test_objects[] = {
    { 0, 0, 25, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24}},
    { 0, 1, 8, { 10, 11, 12, 13, 14, 15, 16, 17 }},
    { 0, 2, 9, { 20, 21, 22, 23, 24, 25, 26, 27, 28 }},
    { 0, 3, 9, { 30, 31, 32, 33, 34, 35, 36, 37, 38 }},
    { 1, 0, 10, { 40, 41, 42, 43, 44, 45, 46, 47, 48, 49 }},
    { 1, 1, 5, { 50, 51, 52, 53, 54 }},
    { 1, 2, 6, { 60, 61, 62, 63, 64, 65 }},
    { 1, 3, 7, { 70, 71, 72, 73, 74, 75, 76 }},
    { 2, 0, 30, { 80, 81, 82, 83, 84, 85, 86, 87, 88, 89,
            90, 91, 92, 93, 94, 95, 96, 97, 98, 99,
            100, 101, 102, 103, 104, 105, 106, 107, 108, 109 }}
};

size_t nb_fragment_test_objects = sizeof(fragment_test_objects) / sizeof(fragment_test_object_t);
size_t nb_fragment_test_groups = 3;
size_t nb_fragment_test_groups_objects[3] = { 4, 4, 1 };

int quicrq_fragment_cache_verify(quicrq_fragment_cached_media_t* cached_ctx)
{
    int ret = 0;
    int nb_fragments_found = 0;
    for (size_t f_id = 0; ret == 0 && f_id < nb_fragment_test_objects; f_id++) {
        size_t offset = 0;
        while (ret == 0 && offset < fragment_test_objects[f_id].length) {
            /* Get fragment at specified offset */

            quicrq_cached_fragment_t* fragment = quicrq_fragment_cache_get_fragment(cached_ctx,
                fragment_test_objects[f_id].group_id, fragment_test_objects[f_id].object_id, offset);
            if (fragment == NULL) {
                DBG_PRINTF("Cannot find fragment, object %zu (%" PRIu64 ",%" PRIu64 ", offset % zu",
                    f_id, fragment_test_objects[f_id].group_id, fragment_test_objects[f_id].object_id, offset);
                ret = -1;
            }
            /* Check that length does not overflow */
            if (ret == 0 && offset + fragment->data_length > fragment_test_objects[f_id].length) {
                DBG_PRINTF("Fragment overflow, object %zu, offset %zu, length %zu", f_id, offset, fragment->data_length);
                ret = -1;
            }
            /* Verify data matches */
            if (ret == 0 && memcmp(fragment_test_objects[f_id].data + offset, fragment->data, fragment->data_length) != 0) {
                DBG_PRINTF("Fragment data incorrect, object %zu, offset %zu, length %zu", f_id, offset, fragment->data_length);
                ret = -1;
            }
            /* Verify is_last_fragment */
            if (ret == 0) {
                int should_be_last = (offset + fragment->data_length) >= fragment_test_objects[f_id].length;
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
            DBG_PRINTF("Found %d fragments, cache contains %d", nb_fragments_found, cached_ctx->fragment_tree.size);
            ret = -1;
        }
    }
    if (ret == 0) {
        /* Verify the chain of fragments */
        quicrq_cached_fragment_t* fragment = cached_ctx->first_fragment;
        quicrq_cached_fragment_t* previous_fragment = NULL;
        int nb_in_chain = 0;
        while (fragment != NULL) {
            nb_in_chain++;
            previous_fragment = fragment;
            fragment = fragment->next_in_order;
        }
        if (nb_in_chain != cached_ctx->fragment_tree.size) {
            DBG_PRINTF("Found %d fragments in chain, cache contains %d", nb_in_chain, cached_ctx->fragment_tree.size);
            ret = -1;
        }
        else if (previous_fragment != cached_ctx->last_fragment) {
            DBG_PRINTF("%s", "Last in chain does not match last fragment");
            ret = -1;
        }
    }
    if (ret == 0) {
        /* verify that the number of objects received matches the expected count */
        if (cached_ctx->nb_object_received != nb_fragment_test_objects) {
            DBG_PRINTF("Received %zu objects instead of %zu", cached_ctx->nb_object_received, nb_fragment_test_objects - 1);
            ret = -1;
        }
    }
    return ret;
}

int quicrq_fragment_cache_fill_test_one(size_t fragment_max, size_t start_object, size_t skip, int nb_pass)
{
    int ret = 0;
    int nb_skipped = 0;
    /* Create a cache */
    quicrq_media_source_ctx_t* srce_ctx = (quicrq_media_source_ctx_t*)malloc(sizeof(quicrq_media_source_ctx_t));
    quicrq_fragment_cached_media_t* cached_ctx = quicrq_fragment_cache_create_ctx(NULL);

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
            for (size_t f_id = 0; ret == 0 && f_id < nb_fragment_test_objects; f_id++) {
                size_t offset = 0;
                while (ret == 0 && offset < fragment_test_objects[f_id].length) {
                    size_t data_length = fragment_test_objects[f_id].length - offset;
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
                        uint64_t nb_objects_previous_group = 0;
                        if (fragment_test_objects[f_id].object_id == 0 && offset == 0 && fragment_test_objects[f_id].group_id > 0) {
                            nb_objects_previous_group = nb_fragment_test_groups_objects[fragment_test_objects[f_id].group_id - 1];
                        }
                        ret = quicrq_fragment_propose_to_cache(cached_ctx, fragment_test_objects[f_id].data + offset,
                            fragment_test_objects[f_id].group_id, fragment_test_objects[f_id].object_id,
                            offset, 0, 0, nb_objects_previous_group, is_last_fragment, data_length, 0);
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
            ret = quicrq_fragment_cache_verify(cached_ctx);
        }
    }

    if (srce_ctx != NULL) {
        free(srce_ctx);
    }

    if (cached_ctx != NULL) {
        /* Delete the cache */
        quicrq_fragment_cache_delete_ctx(cached_ctx);
    }

    return ret;
}

/* For the purpose of simulating the picoquic API, we copy here
 * the definition of the context used by the API
 * `picoquic_provide_datagram_buffer` */
typedef struct st_fragment_test_datagram_buffer_argument_t {
    uint8_t* bytes0; /* Points to the beginning of the encoding of the datagram object */
    uint8_t* bytes; /* Position after encoding the datagram object type */
    uint8_t* bytes_max; /* Pointer to the end of the packet */
    uint8_t* after_data; /* Pointer to end of data written by app */
    size_t allowed_space; /* Data size from bytes to end of packet */
} fragment_test_datagram_buffer_argument_t;

/* Simulate a relay trying to forward data after it is added to the cache. */
int quicrq_fragment_cache_publish_simulate(quicrq_fragment_publisher_context_t* pub_ctx, quicrq_fragment_cached_media_t* cached_ctx_p, 
    uint64_t* sequential_group_id, uint64_t *sequential_object_id, size_t *sequential_offset,
    int is_datagram, uint64_t current_time)
{
    int ret = 0;
    uint8_t data[1024];
    int is_new_group;
    int is_last_fragment;
    int is_media_finished;
    int is_still_active;
    int has_backlog;
    uint64_t group_id = 0;
    uint64_t object_id;
    uint8_t flags = 0;
    uint64_t nb_objects_previous_group;
    size_t fragment_offset = 0;
    uint8_t* fragment = NULL;
    size_t fragment_length;

    do {
        fragment_length = 0;
        if (is_datagram) {
            int at_least_one_active = 0;
            int media_was_sent = 0;
            int not_ready = 0;

            /* Setup a datagram buffer context to mimic picoquic's behavior */
            fragment_test_datagram_buffer_argument_t d_context = { 0 };
            data[0] = 0x30;
            d_context.bytes0 = &data[0];
            d_context.bytes = &data[1];
            d_context.after_data = &data[0];
            d_context.bytes_max = &data[0] + 1024;
            d_context.allowed_space = 1023;
            /* Call the prepare function */
            ret = quicrq_fragment_datagram_publisher_prepare(NULL, pub_ctx, 0, &d_context, d_context.allowed_space,
                &media_was_sent, &at_least_one_active, &not_ready, current_time);
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
                    uint64_t queue_delay;
                    const uint8_t* datagram_max = bytes + datagram_length;

                    bytes = quicrq_datagram_header_decode(bytes, datagram_max, &datagram_stream_id,
                        &group_id, &object_id, &object_offset, &queue_delay, &flags, &nb_objects_previous_group, &is_last_fragment);
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
            /* The first call to the publisher functions positions to the current group id, objectid, offset, etc. */
            nb_objects_previous_group = 0;
            ret = quicrq_fragment_publisher_fn(quicrq_media_source_get_data, pub_ctx, NULL, 1024, &fragment_length,
               &flags, &is_new_group, &is_last_fragment, &is_media_finished, &is_still_active, &has_backlog, current_time);
            if (ret == 0 && fragment_length > 0) {
                group_id = pub_ctx->current_group_id;
                object_id = pub_ctx->current_object_id;
                fragment_offset = pub_ctx->current_offset;
                if (object_id == 0 && fragment_offset == 0) {
                    nb_objects_previous_group = pub_ctx->current_fragment->nb_objects_previous_group;
                }
                if (group_id != *sequential_group_id) {
                    DBG_PRINTF("Expected group id = %" PRIu64 ", got %" PRIu64, *sequential_group_id, group_id);
                    ret = -1;
                }
                if (object_id != *sequential_object_id) {
                    DBG_PRINTF("Expected object id = %" PRIu64 ", got %" PRIu64, *sequential_object_id, object_id);
                    ret = -1;
                }
                if (fragment_offset != *sequential_offset) {
                    DBG_PRINTF("For object id = %" PRIu64 ", expected offset %zu got %zu", object_id, *sequential_offset, fragment_offset);
                    ret = -1;
                }
                if (ret == 0) {
                    /* The second call to the media function copies the data at the required space. */
                    ret = quicrq_fragment_publisher_fn(quicrq_media_source_get_data, pub_ctx, data, 1024, &fragment_length,
                        &flags, &is_new_group, &is_last_fragment, &is_media_finished, &is_still_active, &has_backlog, current_time);
                }
                if (ret == 0){
                    fragment = data;
                    if (is_last_fragment) {
                        *sequential_offset = 0;
                        *sequential_object_id += 1;
                        if (*sequential_group_id < nb_fragment_test_groups &&
                            *sequential_object_id >= nb_fragment_test_groups_objects[*sequential_group_id]) {
                            *sequential_group_id += 1;
                            *sequential_object_id = 0;
                        }
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
        }
        if (ret == 0 && fragment_length > 0) {
            /* submit to the media cache */
            ret = quicrq_fragment_propose_to_cache(cached_ctx_p,
                fragment, group_id, object_id, fragment_offset, 0, flags, nb_objects_previous_group, is_last_fragment, fragment_length, 0);
        }
    } while (ret == 0 && fragment_length > 0);

    return ret;
}

int quicrq_fragment_cache_publish_test_one(int is_datagram)
{
    int ret = 0;
    int nb_skipped = 0;
    /* Create caches and contexts */
    uint64_t current_time = 0;
    uint64_t sequential_group_id = 0;
    uint64_t sequential_object_id = 0;
    size_t sequential_offset = 0;
    quicrq_media_source_ctx_t* srce_ctx = (quicrq_media_source_ctx_t*)malloc(sizeof(quicrq_media_source_ctx_t));
    quicrq_fragment_cached_media_t* cached_ctx = quicrq_fragment_cache_create_ctx(NULL);
    quicrq_media_source_ctx_t* srce_ctx_p = (quicrq_media_source_ctx_t*)malloc(sizeof(quicrq_media_source_ctx_t));
    quicrq_fragment_cached_media_t* cached_ctx_p = quicrq_fragment_cache_create_ctx(NULL);
    quicrq_fragment_publisher_context_t* pub_ctx = (quicrq_fragment_publisher_context_t*)malloc(sizeof(quicrq_fragment_publisher_context_t));
    if (cached_ctx == NULL || srce_ctx == NULL || cached_ctx_p == NULL || srce_ctx_p == NULL || pub_ctx == NULL) {
        ret = -1;
    }
    else {
        memset(srce_ctx, 0, sizeof(quicrq_media_source_ctx_t));
        cached_ctx->srce_ctx = srce_ctx;
        memset(srce_ctx_p, 0, sizeof(quicrq_media_source_ctx_t));
        cached_ctx_p->srce_ctx = srce_ctx_p;
        memset(pub_ctx, 0, sizeof(quicrq_fragment_publisher_context_t));
        pub_ctx->cache_ctx = cached_ctx;
    }

    /* send a first set of segments,
     * starting with designated object */
    for (int pass = 1; ret == 0 && pass <= 2; pass++) {
        size_t skip_count = 0;
        for (size_t f_id = 0; ret == 0 && f_id < nb_fragment_test_objects; f_id++) {
            size_t offset = 0;
            while (ret == 0 && offset < fragment_test_objects[f_id].length) {
                size_t data_length = fragment_test_objects[f_id].length - offset;
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
                    uint64_t nb_objects_previous_group = 0;
                    if (offset == 0 && fragment_test_objects[f_id].group_id > 0 && fragment_test_objects[f_id].object_id == 0) {
                        nb_objects_previous_group = nb_fragment_test_groups_objects[fragment_test_objects[f_id].group_id - 1];
                    }
                    ret = quicrq_fragment_propose_to_cache(cached_ctx, fragment_test_objects[f_id].data + offset,
                        fragment_test_objects[f_id].group_id, fragment_test_objects[f_id].object_id,
                        offset, 0, 0, nb_objects_previous_group, is_last_fragment, data_length, 0);
                    if (ret != 0) {
                        DBG_PRINTF("Proposed segment fails, object %zu, offset %zu, pass %d, ret %d", f_id, offset, pass, ret);
                    }
                    /* Simulate waking up the consumer and polling the data */
                    ret = quicrq_fragment_cache_publish_simulate(pub_ctx, cached_ctx_p, 
                        &sequential_group_id, &sequential_object_id, &sequential_offset,
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
        ret = quicrq_fragment_cache_verify(cached_ctx);
    }

    /* Verify that the consumer cache has the expected content */
    if (ret == 0) {
        ret = quicrq_fragment_cache_verify(cached_ctx_p);
    }

    if (srce_ctx != NULL) {
        free(srce_ctx);
    }

    if (cached_ctx != NULL) {
        /* Delete the cache */
        quicrq_fragment_cache_delete_ctx(cached_ctx);
    }

    if (srce_ctx_p != NULL) {
        free(srce_ctx_p);
    }

    if (cached_ctx_p != NULL) {
        /* Delete the cache */
        quicrq_fragment_cache_delete_ctx(cached_ctx_p);
    }

    if (pub_ctx != NULL) {
        free(pub_ctx);
    }

    return ret;
}

int quicrq_fragment_cache_fill_test()
{
    int ret = 0;
    /* basic test, single pass, nothing skipped, entire objects. */
    if (ret == 0) {
        ret = quicrq_fragment_cache_fill_test_one(100, 0, 0, 1);
        if (ret != 0) {
            DBG_PRINTF("Basic test returns %d", ret);
        }
    }
    /* fragment test, single pass, nothing skipped. */
    if (ret == 0) {
        ret = quicrq_fragment_cache_fill_test_one(8, 0, 0, 1);
        if (ret != 0) {
            DBG_PRINTF("Fragment test returns %d", ret);
        }
    }
    /* fragment test, two passes, skip even. */
    if (ret == 0) {
        ret = quicrq_fragment_cache_fill_test_one(8, 0, 2, 2);
        if (ret != 0) {
            DBG_PRINTF("Skip even test returns %d", ret);
        }
    }
    /* fragment test, two passes, skip odd. */
    if (ret == 0) {
        ret = quicrq_fragment_cache_fill_test_one(8, 0, 1, 2);
        if (ret != 0) {
            DBG_PRINTF("Skip odd test returns %d", ret);
        }
    }
    /* fragment test, three passes, skip odd. */
    if (ret == 0) {
        ret = quicrq_fragment_cache_fill_test_one(8, 0, 1, 3);
        if (ret != 0) {
            DBG_PRINTF("Three passes test returns %d", ret);
        }
    }
    /* Receive test, stream. */
    if (ret == 0) {
        ret = quicrq_fragment_cache_publish_test_one(0);
        if (ret != 0) {
            DBG_PRINTF("Cached stream relay test returns %d", ret);
        }
    }
    /* Receive test, datagram. */
    if (ret == 0) {
        ret = quicrq_fragment_cache_publish_test_one(0);
        if (ret != 0) {
            DBG_PRINTF("Cached datagram relay test returns %d", ret);
        }
    }

    return ret;
}
