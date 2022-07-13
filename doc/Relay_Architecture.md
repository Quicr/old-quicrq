# Architecture of the QUICRQ Relay

The QUICRQ prototype implements the QUICR relay functions. The relays are capable of
receiving data in stream mode or in datagram mode. In both modes, relays will cache
fragments as they arrive. 

```
   +------------+
   | Incoming   |
-->| Stream     |            +------------+
   +-----+------+            | TTL        |
         |                   | Management |
     fragments               +------------|
         |                         |
         |                purge expired records
         |                         |
         |    +--------------------+
         |    |
         v    v
   +------------+
   | Per media  |<-----------------+
   | Cache      |<---+             |
   +------------+    |             |
         |           |             |
      wake up     retrieve         |
      signals     fragments        |
         |           |             |
         |     +-----+------+      |
         |     | Outgoing   |      |
         +---->| Stream     |-->   |
         |     +------------+      |
         |                      retrieve
         |                      fragments
        ...                        |
         |                   +-----+------+
         |                   | Outgoing   |
         +------------------>| Stream     |-->
                             +------------+

```

In all modes, the relays maintain a list of connections that will receive
new fragments when they are ready: connections from clients that have
subscribed to the stream through this relay; and, if the media was
received from a client, a connection to the origin to pass the content of
the client-posted media to the origin. When new fragments are received,
the relevant connections are waken up. As soon as the flow
control and congestion control of the underlying QUIC connections
allow, the fragments will be retrrieved and sent on the relevant
connections.

TTL management will periodically purge the cache records that are too
old, or the cache instances that are not needed anymore.

## Organization of the cache

The relay cache code is in `relay.c`, data structures in `quicrq_relay_internal.h`.
There is a cache instance for each media URL. The cache properties 
include the URL of the media, the identification of the first
available fragment, and the indication of the final fragment if it
has been received. 

Each cached media is modeled as a `quicrq media source`, using the fragment API. The
data for each source is accessed through the media source context
(`quicrq_relay_cached_media_t`). The cached is organized as a list of fragments,
with two modes of access:

* sequential, by order of arrival from the publisher, using a double-linked list
(`first_fragment`, `last_fragment`, `fragment->previous_in_order`,
`fragment->next_in_order`)
* *ordered by group_id, object_id, offset, using a splay
(`fragment_tree`, `fragment->fragment_node`)
The ordering per fragment is done with the comparison
function `quicrq_relay_cache_fragment_node_compare`.

## Cache Creation

The cache is created the first time a client connection refers to the
media URL. This might be:

* A client connection requesting the URL, in which case the relay
  will ask a copy of the media from the origin or the next hop relay
  towards the origin.

* A client connection posting the URL, in which case the relay will
  post a copy of the media towards the origin.

Once the media is available, the relay will learn the starting
group ID and object ID.

## Data Forwarding

Fragments are received from the "incoming" connection. If fragments are
received in stream mode, they will arrive in order. If fragments are received
in datagram mode, fragments may arrive out of order. When receiving in datagram
mode, the media order is used to remove incoming duplicate fragments.
When a non duplicate fragment is received, it is added to the cache, and
the relay functions call `quicrq_source_wakeup` to mark every stream
waiting for the media as "ready", using the function `picoquic_mark_datagram_ready`
for the datagram mode stream, and `picoquic_mark_active_stream` 
for the "stream mode" streams.

Marking the stream ready will cause the "incoming" control streams to be polled
when flow and congestion control allow transmissions. The control streams
will fetch data from the cache, using the "order of arrival" for datagram
mode and the "media order" for stream mode.

In stream mode, the transmission may be delayed until fragments are received
in order. If the last fragment received "fills a hole", that fragment and 
the next available fragments in media order will be forwarded.

## TTL Management

The prototype cache is implemented in memory. This provides for good performance,
but has limited scaling -- data accumulates fairly rapidly when caching video streams.

We have implemented a simple management of the cache time to live. Objects and
fragments in the cache are discarded after two minutes
and whole media streams are discarded 10 seconds after
all fragments have been sent to the subscribed receivers.

The current implementation will delay deleting fragments from the cache if
there are connections actively reading the cache. Each client stream contains
marks of the first group id and object id that has not been fully sent yet.
A fragment will only be removed from the cache if all subscribed streams
have sent the corresponding object.
