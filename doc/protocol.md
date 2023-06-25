# QUICR Protocol in Prototype

[QUICRQ](https://github.com/Quicr/quicrq/) is a prototype implementation of the
QUICR [architecture](https://datatracker.ietf.org/doc/draft-jennings-moq-quicr-arch/)
and [protocol](https://datatracker.ietf.org/doc/draft-jennings-moq-quicr-proto/).
This document describes version "0.21" of the Quicrq protocol, negotiated
using ALPN "quicr-h21".

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

## Control stream

QUICRQ maps a "subscribe" or "post" request to a control stream. When
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

* REQUEST: subscribe to a media, 
* FIN_DATAGRAM: indicates group_id and object_id of last object sent as datagram
* FRAGMENT: carry a media fragment
* POST: publish a media stream towards the origin
* ACCEPT: indicates that the POST requests has been accepted by the next relay or by the origin
* START_POINT: indicates group_id and object_id of first object sent on media stream in response to a request
* CACHE_POLICY: indicates whether to use real time cache pruning or not. 
* SUBSCRIBE: subscribe to an URL pattern
* NOTIFY: announce availabiity of a media with URL matching the pattern


The description of messages in the following subsections use the same conventions as RFC 9000.

### Request Message

The Request message specifies the media requested by a node:

```
quicrq_request_message {
    message_type(i),
    url_length(i),
    url(...),
    media_id(i),
    transport_mode(i),
    intent_mode(i),
    [ start_group_id(i),
      start_object_id(i)]
}
```

The message type will be set to REQUEST (1). The media_id is chosen by the receiver. The
transport mode can be set to one of four values:

* Single Stream (1),
* Warp (2) (not implemented yet)
* Rush (3) (not implemented yet)
* Datagram(4)

The intent mode indicates at which point in the media the receiver wants to start receiving data.
It can be set to:

* `current_group (0)`, starting at the beginning of the current group of objects,
* `next_group (1)`, starting at the beginning of the next group of objects,
* `start_point (2)`, starting at the specified starting group ID and object ID.

The elements `start_group_id` and `start_object_id` are only present
if the intent is set to `start_point`.

### Post Message. 

The POST message is used to indicate intent to publish a media stream:

```
quicrq_post_message { 
    message_type(i),
    url_length(i),
    url(...),
    transport_mode(i),
    cache_policy(8),
    start_group_id(i),
    start_object_id(i)
}
```

The message type will be set to POST (6).
The `transport_mode` is set to the transport mode preferred by the sender.

The `cache_policy` value is set to either `real_time (1)`
or `not real time (0)`. If the cache policy is set to real time,
the relays can purge old media objects from their caches as soon
as they are not any more needed. If not real time, the caches are
only purged when the transmission completes.

The start point for the numbering of group of objects and objects
within groups is set through the parameters `start_group_id` and
`start_object_id`.
_
### Accept Message

The Accept message is sent in response to the Post message, on the server side of
the QUIC control stream. 

```
quicrq_accept_message { 
     message_type(i),
     transport_mode(i),
     [media_id(i)]
}
```

The message id is set to ACCEPT (7). The `transport_mode` flag indicates the transport mode
preferred by the server. In the current build, the `media_id` is only documented if the
transport mode is set to `datagram`._

### Cache Policy Message
 
The Cache Policy message may be sent by the server that received a Request message.
This message is optional: by default, the cache policy is set to `not_real_time`.

```
quicrq_group_policy_message {
    message_type(i),
    cache_policy (8)
}
```
The message id is set to CAHE POLICY (11). 


### Start Point Message
 
The Start Point message indicates the
Group ID and Object ID of the first object that will be sent for the media.
If may be sent by the server that received a Request message. This message is optional: by default,
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
    fragment_offset(i),
    object_length(i),_
    flags (8),
    [nb_objects_previous_group(i)],
    fragment__length(i),
    data(...)
}
```

The message type will be set to FRAGMENT (5).

The offset value indicates where the fragment
data starts in the object designated by `group_id` and `object_id`. Successive messages
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

The `nb_objects_previous_group` is present if and only if this is the first fragment of the first object
in a group, i.e., `object_id` and `offset` are both zero. The number indicates how many objects were sent
in the previous groups. It enables the receiver to check whether all these objects have been received.

The `flags` field is used to maintain low latency by selectively dropping objects in case of congestion.
The value must be the same for all fragments belonging to the same object.
The flags field is encoded as:
```
{
    maybe_dropped(1),
    drop_priority(7)
}
```
The high order bit `maybe_dropped` indicates whether the object can be dropped. The `drop_priority`
allows nodes to selectively drop objects. Objects with the highest priority as dropped first.

When an object is dropped, the relays will send a placeholder, i.e., a single fragment message
in which:

* `offset_and_fin` indicates `offset=0` and `fin=1`
* the `length` is set to zero
* the `flags` field is set to the all-one version `0xff`.

Sending a placeholder allows node to differentiate between a temporary packet loss, which
will be soon corrected, and a deliberate object drop. 


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
The stream is closed when the client is not anymore 
interested by responses to this subscription.
```
quicrq_subscribe_message {
    message_type(i),
    url_length(i),
    url(...)
}
```

The message is sent on a dedicated QUIC Stream. 

The message type will be set to SUBSCRIBE (9)

### Notify Message

The Notify message is sent in response to a Subscribe, when
a media with matching URL becomes available. 
```
quicrq_notify_message {
    message_type(i),
    url_length(i),
    url(...)
}
```
The  message is sent on the stream opened by the Subscribe message.
There may be multiple Notify message sent on that stream.
The stream is closed either after the client closes it, or
if the server is not anymore able to send responses to the
subscription.

The first bytes of the URL must match the URL specified in
the subscription.

The message type will be set to NOTIFY (10)

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

The `nb_objects_previous_group` is present if and only if this is the first fragment of the first object
in a group, i.e., `object_id` and `offset` are both zero. The number indicates how many objects were sent
in the previous groups. It enables receiver to check whether all these objects have been received.

The `flags` field is encoded in exactly the same was as the `flags` field of fragment messages.

### Datagram Repeats

The prototype uses a feature of Picoquic to determine whether a previously sent datagram is probably
lost, and in that case repeats the fragment. The Picoquic feature is susceptible of "false positive"
errors, which means that the same fragment could very well be received multiple times.

Relays may forward fragments even if they arrive out of order.

## Sending objects in Warp streams

When transport mode is set to "Warp", nodes and relays will send objects
in unidirectional QUIC streams, using one stream per "group of objects".
The stream will start with a "warp header" message specifying the
mdeia ID and the group ID, followed for each object in the group by an "object header"
specifying the object ID and the object length and then the content of the objects.

```
+--------+------------+-------+------------+-------+------
| Warp   | Object     | Bytes | Object     | Bytes |
| header | header (0) |  (0)  | header (1) |  (1)  | ...
+--------+------------+-------+------------+-------+------
```

The first object in the stream is object number 0, followed by 1, etc.
Arrival of objects out of order will be treated as a protocol error.

### Warp header

The first message on each Warp header is encoded as:
```
quicrq_warp_header_message {
     message_type(i),
     media_id(i),
     group_id(i)
}
```
The message type is set to WARP_HEADER, 12.

### Object header

Each object in the Warp stream is encoded as an Object header, followed by
the content of the object. The Object header is encoded as:
```
quicrq_object_header_message {
    message_type(i),
    object_id(i),
    [nb_objects_previous_group(i),]
    flags[8],
    object_length(i)
}
```
The message type is set to OBJECT_HEADER, 13.

The `flags` and `nb_objects_previous_group` have the same meaning as
in the fragment header. The `nb_objects_previous_group` is only present
if the `object_id` is set to 0.

The content of the object is encoded in the `object_length` octets that
follow the object header.

## Sending objects in Rush streams

When transport mode is set to "Rush", nodes and relays will send objects
in unidirectional QUIC streams, using one stream per objects.
The stream will start with a "warp header" message specifying the
media ID and the group ID, followed by a single "object header"
and then the content of the objects.

```
+--------+------------+-------+
| Warp   | Object     | Bytes |
| header | header (n) |  (n)  |
+--------+------------+-------+
```

The first object in the stream is object number 0, followed by 1, etc.
Arrival of objects out of order will be treated as a protocol error.

The Warp header and the Object header have the same syntax as for Warp
streams.

When Rush mode is selected, arrival of more than one object per stream
is treated as an error.







# Common Encoding For Media over QUIC Streams

Media data can be sent over QUIC Streams in one of the following ways of grouping the media

- One Stream per Group of Pictures/Group of Obejcts (like with WARP)
- One Stream per frame (like with RUSH)
- One Stream per Track/Rendition (like with QUICR)

In all these cases, following elements are needed to ensure for publishers to produce unique media objects, for relays to group/cache them at segment/group boundaries
and for consumers to request at aggregate as well as individual object level of granularity:
1. TrackId/MediaId/RenditionId : Represents a unique codec bistream and identifies a combination of media type and a set of quality attributes (such as video resolution, framerate, bitrate, profile for video media type)
2. GroupId/SegmentId - Identifies a depedency boundary  (ex. GOP )
3.  ObjectId/MediaElementId - Identifies elements within a group that corresponds to an encoded and encrtypted codec transformation 
4. Flags

## RUSH

For mapping with RUSH, each QUIC Stream shall have the following elements


```
quicrq_media_object {
    message_type(i),
    flags (8),
    object_id(i),
    length(i),
    data(...)
}
```

```
quicrq_media_message {
    message_type(i),
    media_id(i),
    group_id(i),
    quicrq_media_object(...) ...,
}
```

In this case, there exists atmost one ``` quicrq_media_object ```  with the  ```quicrq_media_message```

## Warp

```
quicrq_media_message {
    message_type(i),
    media_id(i),
    group_id(i),
    quicrq_media_object(...) ...,
}
```

In this case, there exists zero or more  ``` quicrq_media_object ``` with the  ```quicrq_media_message```






