/* Handling of a relay
 */
#ifndef QUICRQ_INTERNAL_RELAY_H
#define QUICRQ_INTERNAL_RELAY_H

#include "picosplay.h"
#include "quicrq.h"
#include "quicrq_reassembly.h"
#include "quicrq_internal.h"
#include "quicrq_fragment.h"
#include "quicrq_relay.h"

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

 /* Relay definitions.
  * When acting as a client, the relay adds the media to a cache, which can then be read by the
  * server part of the relay. The publisher state indicates the current object being read from the
  * cache, and the state of the response.
  *
  * In the current version, the cached media is represented in memory by an array of objects,
  * identified by a object number. We may need to add metadata later,such as adding a timestamp
  * to a object, or marking objects as potential restart point, potential skip on restart,
  * and maybe an indication of encoding layer.
  *
  * The objects are added when received on a client connection, organized as a binary tree.
  * The relayed media is kept until the end of the relay connection. This is of course not
  * sustainable, some version of cache management will have to be added later.
  */

typedef struct st_quicrq_relay_consumer_context_t {
    quicrq_ctx_t* qr_ctx;
    quicrq_fragment_cache_t* cache_ctx;
} quicrq_relay_consumer_context_t;

typedef struct st_quicrq_relay_context_t {
    const char* sni;
    struct sockaddr_storage server_addr;
    quicrq_ctx_t* qr_ctx;
    quicrq_cnx_ctx_t* cnx_ctx;
    unsigned int is_origin_only : 1;
    unsigned int use_datagrams : 1;
} quicrq_relay_context_t;

/* Management of the relay cache
 */
uint64_t quicrq_manage_relay_cache(quicrq_ctx_t* qr_ctx, uint64_t current_time);

#endif /* QUICRQ_INTERNAL_RELAY_H */
