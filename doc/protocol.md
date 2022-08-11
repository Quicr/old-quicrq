# QUICR Protocol in Prototype

[QUICRQ](https://github.com/Quicr/quicrq/) is a prototype implementation of the
QUICR [architecture](https://datatracker.ietf.org/doc/draft-jennings-moq-quicr-arch/)
and [protocol](https://datatracker.ietf.org/doc/draft-jennings-moq-quicr-proto/).
This document describes version "0.20a" of the Quicrq protocol, negotiated
using ALPn "quicr-h20".

The prototype implementation has a couple of limitations. It supports
the graph clients and relays described in the archictecture, but for now
only supports a single origin. It supports transmission of media as streams
The prototype implementation had to make practical decisions when mapping
the architecture concepts to QUIC. These decisions include:

* mapping the subscribe and publish processes to QUIC stream
* using a control stream to manage a subscription or post process
* define a post transaction for sending media from client towards origin
* define specific formats for messages sent on streams or in datagrams
* use a "media id" in datagram format instead of hashes
* add messages to indicate start and end points of media streams
* use local feedback from QUIC stack to detect datagram losses
* use a "subscribe" message to request notification of available media URL, which tcan then be used to request that specific media.

## Control stream

QUICRQ maps a "request" or "post" actions to a control stream. When
a client or relay begins a transaction with the relay, the client starts
by opening a new bilateral stream. This stream will act as the "control
channel" for the exchange of data, carrying a series of control messages
in both directions, and also carrying "fragment" messages if the data
is sent in "stream" mode.

The control stream will remain open as long as the peers as still
sending or receiving the media. If either peer closes the control
stream, the other peer will close its end of the stream and discard the state
associated with the media transfer. 

Streams are "one way". If a peer both sends and receive media, there will
be different control streams for sending and receiving.

QUICRQ also maps each "subscribe" action to a separate stream. The relay or
origin server that receives a new stream parses the first message on that
stream to determine whether this is a control stream for managing media
transmission or a subscription stream.

## Sending control messages on streams

The control streams carry series of messages, encoded as a length followed by a message
value:

```
quicrq_message {
    length(16),
    value(...)
}
```
The length is encoded as a 16 bit number in big endian network order.
The prototype uses the following control messages:

* REQUEST_STREAM: subscribe to a media, request data in stream mode
* REQUEST_DATAGRAM: subscribe to a media, request data as datagrams
* FIN_DATAGRAM: indicates group_id and object_id of last object sent as datagram
* FRAGMENT: carry a media fragment
* POST: publish a media stream towards the origin
* ACCEPT: indicates that the POST requests has been accepted by the next relay or by the origin
* START_POINT: indicates group_id and object_id of first object sent on stream
* FIN: indicates the last group_id and object_id for a media
* SUBSCRIBE: subscribe to an URL prefix
* NOTIFY: in response to a SUBSCRIBE, signal that a pattern matching URL is available

The description of messages in the following subsections use the same conventions as RFC 9000.

### Request Message

The Request message specifies the media requested by a node:

```
quicrq_request_message {
    message_type(i),
    url_length(i),
    url(...),
    [datagram_stream_id(i)]
}
```

The message type will be set to REQUEST_STREAM (1) if the client wants to receive the media in
stream mode, or REQUEST_DATAGRAM (2) if receiving in datagram mode. If in datagram mode,
the client must select a datagram stream id that is not yet used for any other media stream.

### Post Message. 

The POST message is used to indicate intent to publish a media stream:

```
quicrq_post_message { 
    message_type(i),
    url_length(i),
    url(...)
    datagram_capable(i)
}
```

The message type will be set to POST (6).
The `datagram_capable` flag is set to 0 if the client can only post data in stream
mode, to 1 if the client is also capable of posting media fragments as datagrams.

### Accept Message

The Accept message is sent in response to the Post message, on the server side of
the QUIC control stream. 

```
quicrq_accept_message { 
     message_type(i),
     use_datagram(i),
     [datagram_stream_id(i)]
}
```

The message id is set to ACCEPT (7). The `use_datagram` flag is set to 0 if the
server wants to receive data in stream mode, and to 1 if the server selects to
receive data fragments as datagrams. In that case, the server must select a
datagram stream id that is not yet used to receive any other media stream.

### Start Point Message
 
The Start Point message indicates the
Group ID and Object ID of the first object that will be sent for the media.
If may be sent by the server that received a Request message, or by the
client that sent a Post message. This message is optional: by default,
media streams start with Group ID and Object ID set to 0.

```
quicrq_start_point_message {
    message_type(i),
    start_group_id(i),
    start_object_id(i)
}
```
The message id is set to START POINT (8). 

### Fragment Message

The Fragment message is used to convey the content of a media stream as a series
of fragments:

```
quicrq_fragment_message {
    message_type(i),
    group_id(i),
    object_id(i),
    offset_and_fin(i),
    flags (8),
    length(i),
    data(...)
 }
```

The message type will be set to FRAGMENT (5). The `offset_and_fin` field encodes
two values, as in:
```
offset_and_fin = 2*offset + is_last_fragment
```
The bit `is_last_fragment` is set to 1 if this fragment is the last one of an object.
The offset value indicates where the fragment
data starts in the object designated by `group_id` and `object_id`.

The `flags` byte encodes the congestion control indicators associated with the object.

Successive messages
are sent in order, which means one of the following three conditions must be verified:

* The group id and object id match the group id and object id of the previous fragment, the
  previous fragment is not a `last fragment`, and the offset
  matches the previous offset plus the previous length.
* The group id matches the group id of the previous message, the object id is equal to
  the object id of the previous fragment plus 1, the offset is 0, and
  the previous message is a `last fragment`.
* The group id matches the group id of the previous message plus 1, the object id is 0,
  the offset is 0, and
  the previous message is a `last fragment`.

NOTE: yes, this is not optimal. Breaking the objects in individual fragments is fine,
  but the group ID, object ID and offset could be inferred from the previous fragments.
  The message could be simplified by carrying just two flags, "is_last_fragment" and
  "is_fist_of_group". A Start Point message could be inserted at the beginning of the
  stream to indicate the initial value of group ID and object ID. Doing that would remove
  6 to 8 bytes of overhead per message.

### Fin Message

The Fragment message indicates the final point of a media stream. 

```
quicrq_fin_message {
    message_type(i),
    final_group_id(i),
    final_object_id(i)
}
```

The message type will be set to FIN (3). The final `group_id` is set to the `group_id`
of the last fragment sent. The final `object_id` is set to the object_id of the last
fragment sent, plus 1. This message is not sent when fragments are sent on stream.

### Subscribe Message

The subscribe message creates a subscription context, asking relay or
origin to notify the client when matching URL become available. 

```
 quicrq_subscribe_message {
    message_type(i),
    url_length(i),
    url(...)
 }
```

The message type will be set to SUBSCRIBE (9).

The message is sent on a newly opened bidirectional stream. The reverse
direction of the stream will be used to receive Notify message. Closing
the subscribe stream cancels the subscription.

### Notify Message

The Notify notifies the client that a new URL is available. It is sent by the
Origin or Relay in response to a Subscribe message. There will be a separate
message for each separate URL that matches the requested prefix.

```
 quicrq_notify_message {
    message_type(i),
    url_length(i),
    url(...)
 }
```

The message type will be set to NOTIFY (10).

## Sending Datagrams

If transmission as datagram is negotiated, the media fragments are sent as
Datagram frames.

### Datagram Header

The datagram frames are encoded as a datagram header, followed by the bytes in
the fragment:

```
datagram_frame_content {
    datagram_header,
    datagram_content
}
```

The datagram header is defined as:

```
quicrq_datagram_header { 
    datagram_stream_id (i)
    group_id (i)
    object_id (i)
    offset_and_fin (i)
    queue_delay (i)
    flags (8)
    [nb_objects_previous_group (i)]
}
```

The datagram_stream_id identifies a specific media stream. The ID is chosen by the receiver of the media stream,
and conveyed by the Request or Accept messages.

The `offset_and_fin` field encodes two values, as in:
```
offset_and_fin = 2*offset + is_last_fragment
```
The `flags` byte encodes the congestion control indicators associated with the object.

The `nb_objects_previous_group` is present if and only if this is the first fragment of the first object
in a group, i.e., `object_id` and `offset` are both zero. The number indicates how many objects were sent
in the previous groups. It enables receiver to check whether all these objects have been received.

### Datagram Repeats

In datagram mode, fragments are sent in datagram frames and may be received
in arbitrary order. Packets carrying datagram frames elicit acknowledgments
in QUIC. The Picoquic stack signals to our QUICR layer whether a previously
sent datagram was acknowledged, or is probably lost. The QUICR stack then
determines whether the fragment needs to be repeated, and if that is the
case the fragment will be repeated later in a new packet. In some
cases the content of a fragment will be repeated as
several fragments, for example if the packet MTU changes between the
initial transmission and the repeat. 

Retransmission will cause out of order arrival of fragments at the relay. It
can also cause fragments to be duplicated, if the initial determination of
loss was spurious.

## Relays and caches

The prototype implements the QUICR relay functions. The relays are capable of
receiving data in stream mode or in datagram mode. In both modes, relays will cache
fragments as they arrive. 

In all modes, the relays maintain a list of connections that will receive
new fragments when they are ready: connections from clients that have
subscribed to the stream through this relay; and, if the media was
received from a client, a connection to the origin to pass the content of
the client-posted media to the origin. When new fragments are received,
they are posted on the relevant connections as soon as the flow
control and congestion control of the underlying QUIC connections
allow.

### Cache and relaying

The prototype relays maintain a separate cache of received fragments for
each media stream that it is processing. If fragments are
received in stream mode, they will arrive in order. If fragments are received
in datagram mode, fragments may arrive out of order.

The cache uses double indexing: by media order, i.e., group_id, object_id,
and fragment offset; and, by order of arrival. When receiving in datagram
mode, the media order is used to remove incoming duplicate fragments.
Fragments that are not duplicate are forwarded to all datagram-mode client
connections as they arrive. Fragments that arrive in order are forwarded
to the stream mode connections; if the fragment "fills a hole", the next
available fragments in media order are also forwarded.

### Out of order relaying

As noted in {{cache-and-relaying}}, fragments that arrive out of order
are relayed immediately. We arrived to that design after trying two
alternatives: insisting on full order before relaying, as is done for
stream mode; or insisting on full reception of all fragments making an
object.

Full order would introduce the same head-of-line blocking
also visible in stream-based relays. In theory, relaying full objects
without requiring that objects be ordered would avoid some of the
head-of-line blocking, but in practice we see that some streams
contain large and small objects, and that losses affecting fragments
of large objects cause almost the same head of line blocking delays
as full ordering. Moreover, if losses happen at several places in the
relay graph, the delays will accumulate. Out of order relaying avoids
these delays.

### Cache cleanup

The prototype cache is implemented in memory. This provides for good
performance, but has limited scaling -- data accumulates fairly
rapidly when caching video streams.

We have implemented a simple management of the cache time to live.
Objects and fragments in the cache are discarded after two minutes,
and whole media streams are discarded 10 seconds after all fragments
have been sent to the subscribed receivers.

## Security issue

This organization makes the implicit assumption that the sender
will promptly repeat all fragments in case of packet loss. This is
a reasonable assumption in the prototype, but it might be a dangerous
assumption in an open system. An attacker could send only
some objects, or maybe some fragments of large objects, and cause
relays and clients to devote large amount of resource to
performing reassembly.

## Congestion control

There are time when the nexthop in a transmission path does not have enough
bandwidth to receive the entire media stream. Attempting to just send it all
would result in increased end-to-end latency, breaking the "real-time"
expectations. This is mitigated by detecting congestion when it happen,
and selectively dropping parts of the media stream to diminish the
network load. Selective dropping is enabled by metadata carried in
object headers, according to a media stream model.

When the congestion happens at the first hop, it can
also be mitigated by modulating the transmission rate. 

### Data Model

Media streams are composed of series of objects, organized in group
of objects. Many media sources use a form of
differential coding, in which an the encoding of an object
includes only differences from previous objects. Such objects can
only be rendered if the previous objects have been received.
Selective dropping must be consistent with the structure of the media:
if an object is dropped, there is little advantage to transmit
dependent objects that could not be rendered.

Media stream also include different categories of objects. For example,
a video stream transmitted at 120 frames per second could also be rendered
in a degraded way at 60 or 30 frame per seconds. If an intermediary has
to drop frames, it might be better to keep some objects rather than others.

We assume that objects will be encrypted, and that intermediaries
cannot access the content of the objects to determine inter-object
dependencies or rendering priorities. The solution is to transmit
unencrypted metadata as part of the media stream. The metadata
includes:

* the organization of the media stream as series of group of objects,
* stream-level metadata indicating general properties of the stream
* object-level metadata indicating the properties of the object.

In the current version, we make the assumption that "group of objects"
form an atomic subset of the media stream, without dependencies on
other groups. This implies that a relay that need to drop data could
drop some or all objects in a group of object, and assume that
receivers will properly render the next group.

In theory, we could have multiple types of streams, with different
sensitivity to congestion. For example, it is common to consider
audio transmission as higher priority than video. In a video game,
updates to the terrain might require different handling than
audio and video. These differences could be encoded as media
level metadata in a further version of the protocol. The current
version will solely rely on object level metadata.

Object level metadata is encoded as an 8 bit field, in which:

* The least significant bit encodes whether the object may be dropped.
* The 7 other bits encode a drop priority level.

If an object is not marked as "may be dropped", it will never be dropped,
even in case of congestion, and even if that causes increases in end-to-end
latency. For all other objects, objects marked as higher drop priority
will be dropped before lower drop priority ones.

### Detecting congestion

Senders and relays know at what time a given object was received,
and can compute at what time it should be sent. Congestion is
detected when that objects cannot be sent before the expected
sending time.

### Reacting to congestion

The relays react to congestion by maintaining a "drop priority level"
for each connection. The drop level is initially set to the lowest
priority, and is reevaluated at regular intervals:

* increase the drop level if the queue has increased beyond the threshold,
* decrease the drop level if the queue is empty
* keep the same level otherwise.

The congestion level is set per connection. A connection may carry
several media streams. The queue is deemed empty of is is empty
for all these media streams, beyond the threshold if this is true
for at least one media stream.

When sending objects and fragments, the following apply:

* if an object is marked as do not drop, don't.
* if the fragment to be sent is the first of an object, drop it if the priority is below the drop level.
* if the fragment is not the first for an object, drop it if the first fragment was dropped.
* if the first object of a group was dropped, drop all other objects.

### Documenting losses

Dropping objects create an ambiguous situation for the receiver. Holes in the
sequence of received objects may be due to transmission errors that will soon be
corrected, or to congestion-driven drops that will never be. The holes must be
somehow documented. The simplest solution is to replace the missing objects
by "hole" messages.

In datagram mode, hole messages are encoded using a special value of the
datagram header, followed by zero bytes of content:

* `datagram_stream_id`, and `group_id` are set to the expected value.
* `object_id` is set to the id of the dropped object.
* `offset_and_fin` are set to `offset=0` and `fin=1`.
* `flags` are set to the special value `0xFF`.
* if needed, `nb_objects_previous_group` is set to the expected value.

This encoding consumes 5 to 8 bytes, which is much lower than a typical object encoding.

In stream mode, hole messages are encoded as a special value of the fragment message,
followed by zero bytes of content:

* `group_id` is set to the expected value.
* `object_id` is set to the id of the dropped object.
* `offset_and_fin` are set to `offset=0` and `fin=1`.
* `flags` are set to the special value `0xFF`.
* `length` is set to zero.