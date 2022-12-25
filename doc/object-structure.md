# Object structure in QUICRQ code

# List of Objects

## QUICRQ Context

`typedef struct st_quicrq_ctx_t quicrq_ctx_t;`

The main context for the QUICRQ stack. There is normally one such context per application.
It manages a Picoquic context, UDP port, etc. It keeps track of connection contexts
and media sources.

## Media Source Context

`typedef struct st_quicrq_media_source_ctx_t quicrq_media_source_ctx_t;`

Describes a media source, which can be either a local source, or a cached source.

## Media Object Source context

`struct st_quicrq_media_object_source_ctx_t`

Describes an original source, published locally.

## Connection Context

`typedef struct st_quicrq_cnx_ctx_t quicrq_cnx_ctx_t;`

Manages a connection between this and another node. Maps to a QUIC connection.

## Stream Context

`typedef struct st_quicrq_stream_ctx_t quicrq_stream_ctx_t;`

Control stream context for a media stream. The actual media will be sent on
either that stream (in stream mode), or as a series of datagrams (in datagram mode)
or as a series of unidirectional streams (in warp mode)

If sending datagrams, the stream context includes a list of Datagram ACK State
used to manage the acknowledgement and retransmission of datagrams:

`typedef struct st_quicrq_datagram_ack_state_t quicrq_datagram_ack_state_t`

If sending or receiving in WARP mode, the stream context manages a list
of unidirectional stream contexts, one per group of objects.

Sending streams are associated with a Media Source Context, from which they get the data.

For publishing streams, this is mediated by a `media context`

## Media Context

`typedef struct st_quicrq_fragment_publisher_context_t quicrq_fragment_publisher_context_t;`

# Relation between the objects

```
+----------------+
| QUICRQ Context |
+----------+-+-+-+
           | | |
           | | |            +---------------------+
           | | |          +---------------------+ |
           | | |        +---------------------+ |-+
           | | +------->| Media object source |-+
           | |          +---------------------+
           | |              +--------------+
           | |            +--------------+ |
           | |          +--------------+ |-+
           | +--------->| Media source |-+
           |            +----------+---+
           |                       |  ^
           |                       |  |
           |                       |  +-------------------------+
           |                       |        +----------------+  |
           |                       +--------+ Fragment cache |  |
           |                                +----------------+  |
           |                                           ^        |
           |       +-------------------+               |        |
           |     +-------------------+ |               |        |
           |   +-------------------+ |-+               |        |
           +-->|Connection context |-+                 |        |
               +-+-+---------------+                   |        |
                 | |            +----------------+     |        |
                 | |          +----------------+ |     }        |
                 | |        +----------------+ |-+     |        |
                 | +------->| Stream context |-+       |        |
                 |          +-+-+----+--+-+--+         |        |
                 |            | |    |  | |            |        |
       +---------+---------+  | |    |  | +------------+        |
     +-------------------+ |  | |    |  +-----------------------+
   +-------------------+ | +  | |    |   if sending media,
   | Uni stream Context| +    | |    |   or receiving at relay
   +-----------------+-+      | |    |
    if in Warp mode  |        | |    |  +--------------------+
                     +--------+ |    +--+ Reassembly context |
       +-------------------+    |       +--------------------+
     +-------------------+ |    |       if receiving at client
   +-------------------+ |-+    |
   | Datagram ACK ctx  |-+      |
   +-----------------+-+        |
    if sending       |          |   
    datagrams        +----------+ 
```                              


