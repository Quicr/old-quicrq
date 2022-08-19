/* Handling of a relay */
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include "picoquic_utils.h"
#include "picosplay.h"
#include "quicrq.h"
#include "quicrq_reassembly.h"
#include "quicrq_internal.h"
#include "quicrq_fragment.h"
#include "quicrq_relay_internal.h"

/* A relay is a specialized node, acting both as client when acquiring a media
 * fragment and as server when producing data.
 * 
 * There is one QUICRQ context per relay, used both for initiating a connection to
 * the server, and accepting connections from the client.
 * 
 * When a client requests an URL from the relay, the relay checks whether that URL is
 * already published, i.e., present in the local cache. If it is, then the client is
 * connected to that source. If not, the source is created and a request to the
 * server is started, in order to acquire the URL.
 * 
 * When  a client posts an URL to the relay, the relay checks whether the URL exists
 * already. For now, we will treat that as an error case. If it does not, the
 * relay creates a context over which to receive the media, and POSTs the content to
 * the server.
 * 
 * The client half creates a list of media objects. For simplification, the server half will
 * only deal with the media objects that are fully received. When a media object is
 * fully received, it becomes available. We may consider a difference in
 * availability between "in-order" and "out-of-sequence" availablity, which
 * may need to be reflected in the contract between connection and sources.
 */

int quicrq_relay_consumer_cb(
    quicrq_media_consumer_enum action,
    void* media_ctx,
    uint64_t current_time,
    const uint8_t* data,
    uint64_t group_id,
    uint64_t object_id,
    uint64_t offset,
    uint64_t queue_delay,
    uint8_t flags,
    uint64_t nb_objects_previous_group,
    int is_last_fragment,
    size_t data_length)
{
    int ret = 0;
    quicrq_relay_consumer_context_t * cons_ctx = (quicrq_relay_consumer_context_t*)media_ctx;

    switch (action) {
    case quicrq_media_datagram_ready:
        /* Check that this datagram was not yet received.
         * This requires accessing the cache by object_id, offset and length. */
         /* Add fragment (or fragments) to cache */
        ret = quicrq_fragment_propose_to_cache(cons_ctx->cached_ctx, data, 
            group_id, object_id, offset, queue_delay, flags, nb_objects_previous_group, is_last_fragment, data_length, current_time);
        /* Manage fin of transmission */
        if (ret == 0) {
            /* If the final group id and object id are known, and the next expected
             * values match, then the transmission is finished. */
            if ((cons_ctx->cached_ctx->final_group_id > 0 || cons_ctx->cached_ctx->final_object_id > 0) &&
                cons_ctx->cached_ctx->next_group_id == cons_ctx->cached_ctx->final_group_id &&
                cons_ctx->cached_ctx->next_object_id == cons_ctx->cached_ctx->final_object_id) {
                ret = quicrq_consumer_finished;
            }
        }
        break;
    case quicrq_media_final_object_id:
        /* Document the final group-ID and object-ID in context */
        ret = quicrq_fragment_cache_learn_end_point(cons_ctx->cached_ctx, group_id, object_id);
        if (ret == 0) {
            /* Manage fin of transmission on the consumer connection */
            if (cons_ctx->cached_ctx->next_group_id == cons_ctx->cached_ctx->final_group_id &&
                cons_ctx->cached_ctx->next_object_id == cons_ctx->cached_ctx->final_object_id) {
                ret = quicrq_consumer_finished;
            }
        }
        break;
    case quicrq_media_start_point:
        /* Document the start point, and clean the cache of data before that point */
        ret = quicrq_fragment_cache_learn_start_point(cons_ctx->cached_ctx, group_id, object_id);
        break;
    case quicrq_media_close:
        /* Document the final object */
        if (cons_ctx->cached_ctx->final_group_id == 0 && cons_ctx->cached_ctx->final_object_id == 0) {
            /* cache delete time set in the future to allow for reconnection. */
            cons_ctx->cached_ctx->cache_delete_time = current_time + 30000000;
            /* Document the last group_id and object_id that were fully received. */
            if (cons_ctx->cached_ctx->next_offset == 0) {
                cons_ctx->cached_ctx->final_group_id = cons_ctx->cached_ctx->next_group_id;
                cons_ctx->cached_ctx->final_object_id = cons_ctx->cached_ctx->next_object_id;
            }
            else  if (cons_ctx->cached_ctx->next_object_id > 1) {
                cons_ctx->cached_ctx->final_group_id = cons_ctx->cached_ctx->next_group_id;
                cons_ctx->cached_ctx->final_object_id = cons_ctx->cached_ctx->next_object_id - 1;
            }
            else {
                /* find the last object that was fully received. If there is none,
                 * leave the final_group_id and final_object_id
                 */
                quicrq_cached_fragment_t key = { 0 };
                picosplay_node_t* fragment_node = NULL;
                quicrq_cached_fragment_t* fragment = NULL;

                key.group_id = cons_ctx->cached_ctx->next_group_id;
                key.object_id = 0;
                key.offset = 0;
                fragment_node = picosplay_find_previous(&cons_ctx->cached_ctx->fragment_tree, &key);
                if (fragment_node != NULL) {
                    fragment = (quicrq_cached_fragment_t*)quicrq_fragment_cache_node_value(fragment_node);
                }
                if (fragment != NULL) {
                    cons_ctx->cached_ctx->final_group_id = fragment->group_id;
                    cons_ctx->cached_ctx->final_object_id = fragment->object_id;
                }
                else {
                    cons_ctx->cached_ctx->final_group_id = cons_ctx->cached_ctx->first_group_id;
                    cons_ctx->cached_ctx->final_object_id = cons_ctx->cached_ctx->first_object_id;
                }
            }
        }
        else {
            /* cache delete time set at short interval. */
            cons_ctx->cached_ctx->cache_delete_time = current_time + 3000000;
        }
        cons_ctx->cached_ctx->is_closed = 1;
        
        /* Set the target delete date */
        /* Notify consumers of the stream */
        quicrq_source_wakeup(cons_ctx->cached_ctx->srce_ctx);
        /* Free the media context resource */
        free(media_ctx);
        break;

    default:
        ret = -1;
        break;
    }
    return ret;
}

/* Server part of the relay.
 * The publisher functions tested at client and server delivers data in sequence.
 * We can do that as a first approximation, but proper relay handling needs to consider
 * delivering data out of sequence too.
 * Theory of interaction:
 * - The client calls for "in sequence data"
 * - If there is some, proceed as usual.
 * - If there is a hole in the sequence, inform of the hole.
 * Upon notification of a hole, the client may either wait for the inline delivery,
 * so everything is sent in sequence, or accept out of sequence transmission.
 * If out of sequence transmission is accepted, the client starts polling
 * for the new object-id, offset zero.
 * When the correction is available, the client is notified, and polls for the
 * missing object-id.
 */

/* Default source is called when a client of a relay is loading a not-yet-cached
 * URL. This requires creating the desired URL, and then opening the stream to
 * the server. Possibly, starting a connection if there is no server available.
 */

int quicrq_relay_check_server_cnx(quicrq_relay_context_t* relay_ctx, quicrq_ctx_t* qr_ctx)
{
    int ret = 0;
    /* If there is no valid connection to the server, create one. */
    /* TODO: check for expiring connection */
    if (relay_ctx->cnx_ctx == NULL) {
        relay_ctx->cnx_ctx = quicrq_create_client_cnx(qr_ctx, relay_ctx->sni,
            (struct sockaddr*)&relay_ctx->server_addr);
    }
    if (relay_ctx->cnx_ctx == NULL) {
        ret = -1;
    }
    return ret;
}

quicrq_relay_consumer_context_t* quicrq_relay_create_cons_ctx()
{
    quicrq_relay_consumer_context_t* cons_ctx = (quicrq_relay_consumer_context_t*)
        malloc(sizeof(quicrq_relay_consumer_context_t));
    if (cons_ctx != NULL) {
        memset(cons_ctx, 0, sizeof(quicrq_relay_consumer_context_t));
    }
    return cons_ctx;
}

int quicrq_relay_default_source_fn(void* default_source_ctx, quicrq_ctx_t* qr_ctx,
    const uint8_t* url, const size_t url_length)
{
    int ret = 0;
    /* Should there be a single context type for relays and origin? */
    quicrq_relay_context_t* relay_ctx = (quicrq_relay_context_t*)default_source_ctx;
    if (url == NULL) {
        /* By convention, this is a request to release the resource of the origin */
        quicrq_set_default_source(qr_ctx, NULL, NULL);
    }
    else {
        quicrq_fragment_cached_media_t* cache_ctx = quicrq_fragment_cache_create_ctx(qr_ctx);
        quicrq_relay_consumer_context_t* cons_ctx = NULL;
        if (cache_ctx == NULL) {
            ret = -1;
        }
        else if (!relay_ctx->is_origin_only) {
            /* If there is no valid connection to the server, create one. */
            ret = quicrq_relay_check_server_cnx(relay_ctx, qr_ctx);

            if (ret == 0) {
                /* Create a consumer context for the relay to server connection */
                cons_ctx = quicrq_relay_create_cons_ctx();

                if (cons_ctx == NULL) {
                    ret = -1;
                }
                else {
                    cons_ctx->cached_ctx = cache_ctx;

                    /* Request a URL on a new stream on that connection */
                    ret = quicrq_cnx_subscribe_media(relay_ctx->cnx_ctx, url, url_length,
                        relay_ctx->use_datagrams, quicrq_relay_consumer_cb, cons_ctx);
                    if (ret == 0){
                        /* Document the stream ID for that cache */
                        char buffer[256];
                        cache_ctx->subscribe_stream_id = relay_ctx->cnx_ctx->last_stream->stream_id; 
                        picoquic_log_app_message(relay_ctx->cnx_ctx->cnx, "Asking server for URL: %s on stream %" PRIu64,
                            quicrq_uint8_t_to_text(url, url_length, buffer, 256), cache_ctx->subscribe_stream_id);
                    }
                }
            }
        }
        else {
            /* TODO: whatever is needed for origin only behavior. */
        }

        if (ret == 0) {
            /* if succeeded, publish the source */
            ret = quicrq_publish_fragment_cached_media(qr_ctx, cache_ctx, url, url_length);
        }

        if (ret != 0) {
            if (cache_ctx != NULL) {
                free(cache_ctx);
            }
            if (cons_ctx != NULL) {
                free(cons_ctx);
            }
        }
    }
    return ret;
}

/* The relay consumer callback is called when receiving a "post" request from
 * a client. It will initialize a cached media context for the posted url.
 * The media will be received on the specified stream, as either stream or datagram.
 * The media shall be stored in a local cache entry.
 * The cached entry shall be pushed on a connection to the server.
 */

int quicrq_relay_consumer_init_callback(quicrq_stream_ctx_t* stream_ctx, const uint8_t* url, size_t url_length)
{
    int ret = 0;
    quicrq_ctx_t* qr_ctx = stream_ctx->cnx_ctx->qr_ctx;
    quicrq_relay_context_t* relay_ctx = (quicrq_relay_context_t*)qr_ctx->default_source_ctx;

    quicrq_fragment_cached_media_t* cache_ctx = NULL;
    quicrq_relay_consumer_context_t* cons_ctx = NULL;

    /* If there is no valid connection to the server, create one. */
    ret = quicrq_relay_check_server_cnx(relay_ctx, qr_ctx);
    if (ret == 0) {
        quicrq_media_source_ctx_t* srce_ctx = quicrq_find_local_media_source(qr_ctx, url, url_length);

        if (srce_ctx != NULL) {
            cache_ctx = (quicrq_fragment_cached_media_t*)srce_ctx->pub_ctx;
            if (cache_ctx == NULL) {
                ret = -1;
            }
            else {
                /* Abandon the stream that was open to receive the media */
                char buffer[256];
                quicrq_cnx_abandon_stream_id(relay_ctx->cnx_ctx, cache_ctx->subscribe_stream_id);
                picoquic_log_app_message(stream_ctx->cnx_ctx->cnx, "Abandon subscription to URL: %s",
                    quicrq_uint8_t_to_text(url, url_length, buffer, 256));
            }
        }
        else {
            /* Create a cache context for the URL */
            cache_ctx = quicrq_fragment_cache_create_ctx(qr_ctx);
            if (cache_ctx != NULL) {
                char buffer[256];
                ret = quicrq_publish_fragment_cached_media(qr_ctx, cache_ctx, url, url_length);
                picoquic_log_app_message(stream_ctx->cnx_ctx->cnx, "Create cache for URL: %s",
                    quicrq_uint8_t_to_text(url, url_length, buffer, 256));
                if (ret != 0) {
                    /* Could not publish the media, free the resource. */
                    free(cache_ctx);
                    cache_ctx = NULL;
                    ret = -1;
                }
            }
        }

        if (ret == 0) {
            cons_ctx = quicrq_relay_create_cons_ctx();
            if (cons_ctx == NULL) {
                ret = -1;
            }
            else {
                ret = quicrq_cnx_post_media(relay_ctx->cnx_ctx, url, url_length, relay_ctx->use_datagrams);
                if (ret != 0) {
                    /* TODO: unpublish the media context */
                    DBG_PRINTF("Should unpublish media context, ret = %d", ret);
                }
                else {
                    /* set the parameter in the stream context. */
                    char buffer[256];

                    cons_ctx->cached_ctx = cache_ctx;
                    ret = quicrq_set_media_stream_ctx(stream_ctx, quicrq_relay_consumer_cb, cons_ctx);
                    picoquic_log_app_message(stream_ctx->cnx_ctx->cnx, "Posting URL: %s to server on stream %" PRIu64,
                        quicrq_uint8_t_to_text(url, url_length, buffer, 256), stream_ctx->stream_id);
                }
            }
        }
    }

    return ret;
}



/* Management of subscriptions on relays.
 *
 * Every subscription managed from a client should have a corresponding subscription
 * request from the relay to the origin.
 */

int quicrq_relay_subscribe_notify(void* notify_ctx, const uint8_t* url, size_t url_length)
{
    int ret = 0;
    /* Retrieve the relay context */
    quicrq_ctx_t* qr_ctx = (quicrq_ctx_t*)notify_ctx;
    /* Find whether there is already a source with that name */
    quicrq_media_source_ctx_t* srce_ctx = qr_ctx->first_source;

    while (srce_ctx != NULL) {
        if (srce_ctx->media_url_length == url_length &&
            memcmp(srce_ctx->media_url, url, url_length) == 0) {
            break;
        }
        srce_ctx = srce_ctx->next_source;
    }
    if (srce_ctx == NULL) {
        /* If there is not, add the corresponding file to the catch, as
         * if a subscribe to a file had been received. */
        ret = quicrq_relay_default_source_fn(qr_ctx->relay_ctx, qr_ctx, url, url_length);
    }

    return ret;
}

quicrq_stream_ctx_t* quicrq_relay_find_subscription(quicrq_ctx_t* qr_ctx, const uint8_t* url, size_t url_length)
{
    quicrq_stream_ctx_t* stream_ctx = NULL;
    /* Locate the connection to the origin */
    if (qr_ctx->relay_ctx->cnx_ctx != NULL) {
        stream_ctx = qr_ctx->relay_ctx->cnx_ctx->first_stream;
        while (stream_ctx != NULL) {
            if (stream_ctx->subscribe_prefix != NULL &&
                stream_ctx->subscribe_prefix_length == url_length &&
                memcmp(stream_ctx->subscribe_prefix, url, url_length) == 0) {
                break;
            }
            stream_ctx = stream_ctx->next_stream;
        }
    }
    return stream_ctx;
}

void quicrq_relay_subscribe_pattern(quicrq_ctx_t* qr_ctx, quicrq_subscribe_action_enum action, const uint8_t* url, size_t url_length)
{
    if (action == quicrq_subscribe_action_unsubscribe) {
        if (qr_ctx->relay_ctx->cnx_ctx != NULL) {
            /* Check whether there is still a client connection subscribed to this pattern */
            quicrq_cnx_ctx_t* cnx_ctx = qr_ctx->first_cnx;
            int is_subscribed = 0;
            while (cnx_ctx != NULL && !is_subscribed) {
                /* Only examine the connections to this relay */
                if (cnx_ctx->is_server) {
                    quicrq_stream_ctx_t* stream_ctx = cnx_ctx->first_stream;
                    while (stream_ctx != NULL) {
                        if (stream_ctx->send_state == quicrq_notify_ready &&
                            stream_ctx->subscribe_prefix_length == url_length &&
                            memcmp(stream_ctx->subscribe_prefix, url, url_length) == 0) {
                            is_subscribed = 1;
                            break;
                        }
                        stream_ctx = stream_ctx->next_stream;
                    }
                }
            }
            /* Find the outgoing stream for that pattern and close it. */
            if (is_subscribed) {
                quicrq_stream_ctx_t* stream_ctx = quicrq_relay_find_subscription(qr_ctx, url, url_length);
                if (stream_ctx != NULL) {
                    int ret = quicrq_cnx_subscribe_pattern_close(qr_ctx->relay_ctx->cnx_ctx, stream_ctx);
                    if (ret != 0) {
                        char buffer[256];
                        quicrq_log_message(qr_ctx->relay_ctx->cnx_ctx, "Cannot unsubscribe relay from origin for %s*",
                            quicrq_uint8_t_to_text(url, url_length, buffer, 256));
                    }
                }
            }
        }
    }
    else if (action == quicrq_subscribe_action_subscribe) {
        /* new subscription from a client. Check the current connection to
         * see whether a matching subscription exists.*/
         /* If no connection to the server yet, create one */
        if (quicrq_relay_check_server_cnx(qr_ctx->relay_ctx, qr_ctx) != 0) {
            DBG_PRINTF("%s", "Cannot create a connection to the origin");
        }
        else {
            quicrq_stream_ctx_t* stream_ctx = quicrq_relay_find_subscription(qr_ctx, url, url_length);
            if (stream_ctx == NULL) {
                /* No subscription, create one. */
                stream_ctx = quicrq_cnx_subscribe_pattern(qr_ctx->relay_ctx->cnx_ctx, url, url_length,
                    quicrq_relay_subscribe_notify, qr_ctx);
            }

            if (stream_ctx == NULL) {
                char buffer[256];
                quicrq_log_message(qr_ctx->relay_ctx->cnx_ctx, "Cannot subscribe from relay to origin for %s*",
                    quicrq_uint8_t_to_text(url, url_length, buffer, 256));
            }
        }
    }
}

/* The relay functionality has to be established to add the relay
 * function to a QUICRQ node.
 */
int quicrq_enable_relay(quicrq_ctx_t* qr_ctx, const char* sni, const struct sockaddr* addr, int use_datagrams)
{
    int ret = 0;

    if (qr_ctx->relay_ctx != NULL) {
        /* Error -- cannot enable relaying twice without first disabling it */
        ret = -1;
    }
    else {
        size_t sni_len = (sni == NULL) ? 0 : strlen(sni);
        quicrq_relay_context_t* relay_ctx = (quicrq_relay_context_t*)malloc(
            sizeof(quicrq_relay_context_t) + sni_len + 1);
        if (relay_ctx == NULL) {
            ret = -1;
        }
        else {
            /* initialize the relay context. */
            uint8_t* v_sni = ((uint8_t*)relay_ctx) + sizeof(quicrq_relay_context_t);
            memset(relay_ctx, 0, sizeof(quicrq_relay_context_t));
            picoquic_store_addr(&relay_ctx->server_addr, addr);
            if (sni_len > 0) {
                memcpy(v_sni, sni, sni_len);
            }
            v_sni[sni_len] = 0;
            relay_ctx->sni = (char const*)v_sni;
            relay_ctx->use_datagrams = use_datagrams;
            /* set the relay as default provider */
            quicrq_set_default_source(qr_ctx, quicrq_relay_default_source_fn, relay_ctx);
            /* set a default post client on the relay */
            quicrq_set_media_init_callback(qr_ctx, quicrq_relay_consumer_init_callback);
            qr_ctx->relay_ctx = relay_ctx;
            qr_ctx->manage_relay_cache_fn = quicrq_manage_relay_cache;
            qr_ctx->manage_relay_subscribe_fn = quicrq_relay_subscribe_pattern;
        }
    }
    return ret;
}

void quicrq_disable_relay(quicrq_ctx_t* qr_ctx)
{
    if (qr_ctx->relay_ctx != NULL) {
        free(qr_ctx->relay_ctx);
        qr_ctx->relay_ctx = NULL;
        qr_ctx->manage_relay_cache_fn = NULL;
    }
}

/* Management of the relay cache.
 * Ensure that old segments are removed.
 */
uint64_t quicrq_manage_relay_cache(quicrq_ctx_t* qr_ctx, uint64_t current_time)
{
    uint64_t next_time = UINT64_MAX;

    if (qr_ctx->relay_ctx != NULL && (qr_ctx->cache_duration_max > 0 || qr_ctx->is_cache_closing_needed)) {
        int is_cache_closing_still_needed = 0;
        quicrq_media_source_ctx_t* srce_ctx = qr_ctx->first_source;

        /* Find all the sources that are cached by the relay function */
        while (srce_ctx != NULL) {
            quicrq_media_source_ctx_t* srce_to_delete = NULL;
            if (srce_ctx->subscribe_fn == quicrq_fragment_publisher_subscribe &&
                srce_ctx->getdata_fn == quicrq_fragment_publisher_fn &&
                srce_ctx->get_datagram_fn == quicrq_fragment_datagram_publisher_fn &&
                srce_ctx->delete_fn == quicrq_fragment_publisher_delete) {
                /* This is a source created by the relay */
                quicrq_fragment_cached_media_t* cache_ctx = (quicrq_fragment_cached_media_t*)srce_ctx->pub_ctx;

                if (qr_ctx->cache_duration_max > 0) {
                    /* TODO: Check the lowest value of the published object along subscribed clients;
                     * setting the lowest value to UIN64_MAX for now */
                     /* Purge cache from old entries */
                    quicrq_fragment_cache_media_purge(cache_ctx,
                        current_time, qr_ctx->cache_duration_max, UINT64_MAX);
                }
                if (cache_ctx->is_closed) {
                    if (cache_ctx->first_fragment == NULL) {
                        /* If the cache is empty and the source is closed, schedule it for deletion. */
                        srce_to_delete = srce_ctx;
                    } else if (srce_ctx->first_stream == NULL) {
                        /* If the source is closed and has no reader, delete at scheduled time. */
                        if (current_time >= cache_ctx->cache_delete_time) {
                            srce_to_delete = srce_ctx;
                        }
                        else if (cache_ctx->cache_delete_time < next_time) {
                            /* Not ready to delete yet, ask for a wake up on timer */
                            next_time = cache_ctx->cache_delete_time;
                            is_cache_closing_still_needed = 1;
                        }
                    }
                }
            }
            srce_ctx = srce_ctx->next_source;
            if (srce_to_delete != NULL) {
                quicrq_delete_source(srce_to_delete, qr_ctx);
            }
        }
        qr_ctx->is_cache_closing_needed = is_cache_closing_still_needed;
    }

    return next_time;
}

/*
 * The origin server behavior is very similar to the behavior of a relay, but
 * there are some key differences:
 *  
 *  1) When receiving a "subscribe" request, the relay creates the media context and
 *     starts a connection. The server creates a media context but does not start the connection. 
 *  2) When receiving a "post" request, the relay creates a cache version and also
 *     forwards it to the server using an upload connection. There is no upload
 *     connection at the origin server. 
 *  3) When receiving a "post" request, the server must check whether the media context 
 *     already exists, and if it does connects it.
 */

int quicrq_origin_consumer_init_callback(quicrq_stream_ctx_t* stream_ctx, const uint8_t* url, size_t url_length)
{
    int ret = 0;
    quicrq_ctx_t* qr_ctx = stream_ctx->cnx_ctx->qr_ctx;
    quicrq_fragment_cached_media_t* cache_ctx = NULL;
    quicrq_relay_consumer_context_t* cons_ctx = quicrq_relay_create_cons_ctx();
    char buffer[256];

    if (cons_ctx == NULL) {
        ret = -1;
    } else {
        /* Check whether there is already a context for this media */
        quicrq_media_source_ctx_t* srce_ctx = quicrq_find_local_media_source(qr_ctx, url, url_length);

        if (srce_ctx != NULL) {
            cache_ctx = (quicrq_fragment_cached_media_t*)srce_ctx->pub_ctx;
            picoquic_log_app_message(stream_ctx->cnx_ctx->cnx, "Found cache context for URL: %s",
                quicrq_uint8_t_to_text(url, url_length, buffer, 256));
        }
        else {
            /* Create a cache context for the URL */
            cache_ctx = quicrq_fragment_cache_create_ctx(qr_ctx);
            if (cache_ctx != NULL) {
                ret = quicrq_publish_fragment_cached_media(qr_ctx, cache_ctx, url, url_length);
                if (ret != 0) {
                    /* Could not publish the media, free the resource. */
                    free(cache_ctx);
                    cache_ctx = NULL;
                    picoquic_log_app_message(stream_ctx->cnx_ctx->cnx, "Cannot create cache for URL: %s",
                        quicrq_uint8_t_to_text(url, url_length, buffer, 256));
                }
                else {
                    picoquic_log_app_message(stream_ctx->cnx_ctx->cnx, "Created cache context for URL: %s",
                        quicrq_uint8_t_to_text(url, url_length, buffer, 256));
                }
            }
        }

        if (ret == 0) {
            /* set the parameter in the stream context. */
            cons_ctx->cached_ctx = cache_ctx;
            ret = quicrq_set_media_stream_ctx(stream_ctx, quicrq_relay_consumer_cb, cons_ctx);
        }

        if (ret != 0) {
            free(cons_ctx);
        }
    }
    return ret;
}




int quicrq_enable_origin(quicrq_ctx_t* qr_ctx, int use_datagrams)
{
    int ret = 0;
    quicrq_relay_context_t* relay_ctx = (quicrq_relay_context_t*)malloc(
        sizeof(quicrq_relay_context_t));
    if (relay_ctx == NULL) {
        ret = -1;
    }
    else {
        /* initialize the relay context. */
        memset(relay_ctx, 0, sizeof(quicrq_relay_context_t));
        relay_ctx->use_datagrams = use_datagrams;
        relay_ctx->is_origin_only = 1;
        /* set the relay as default provider */
        quicrq_set_default_source(qr_ctx, quicrq_relay_default_source_fn, relay_ctx);
        /* set a default post client on the relay */
        quicrq_set_media_init_callback(qr_ctx, quicrq_origin_consumer_init_callback);
        /* Remember pointer */
        qr_ctx->relay_ctx = relay_ctx;
        /* Set the cache function */
        qr_ctx->manage_relay_cache_fn = quicrq_manage_relay_cache;
    }
    return ret;
}
