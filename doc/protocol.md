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

* REQUEST_STREAM: subscribe to a media, request data in stream mode
* REQUEST_DATAGRAM: subscribe to a media, request data as datagrams
* FIN_DATAGRAM: indicates group_id and object_id of last object sent as datagram
* FRAGMENT: carry a media fragment
* POST: publish a media stream towards the origin
* ACCEPT: indicates that the POST requests has been accepted by the next relay or by the origin
* START_POINT: indicates group_id and object_id of first object sent on stream

The description of messages in the following subsections use the same conventions as RFC 9000.

### Request Message

The Request message specifies the media requested by a node:

```
quicrq_request_message {
 *     message_type(i),
 *     url_length(i),
 *     url(...),
 *     [datagram_stream_id(i)]
 * }
```

The message type will be set to REQUEST_STREAM (1) if the client wants to receive the media in
stream mode, or REQUEST_DATAGRAM (2) if receiving in datagram mode. If in datagram mode,
the client must select a datagram stream id that is not yet used for any other media stream.

### Post Message. 

The POST message is used to indicate intent to publish a media stream:

```
quicrq_post_message { 
 *     message_type(i),
 *     url_length(i),
 *     url(...)
 *     datagram_capable(i)
 * }
```

The message type will be set to POST (6).
The `datagram_capable` flag is set to 0 if the client can only post data in stream
mode, to 1 if the client is also capable of posting media fragments as datagrams.

### Accept Message

The Accept message is sent in response to the Post message, on the server side of
the QUIC control stream. 

```
quicrq_accept_message { 
  *     message_type(i),
  *     use_datagram(i),
  *     [datagram_stream_id(i)]
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
 * quicrq_start_point_message {
 *     message_type(i),
 *     start_group_id(i),
 *     start_object_id(i)
 * }
```
The message id is set to START POINT (8). 

### Fragment Message

The Fragment message is used to convey the content of a media stream as a series
of fragments:

```
quicrq_fragment_message {
 *     message_type(i),
 *     group_id(i),
 *     object_id(i),
 *     offset_and_fin(i),
 *     length(i),
 *     data(...)
 }
```

The message type will be set to FRAGMENT (5). The `offset_and_fin` field encodes
two values, as in:
```
offset_and_fin = 2*offset + is_last_fragment
```
The flag `is_last_fragment` is set to 1 if this fragment is the last one of an object.
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

NOTE: yes, this is not optimal. Breaking the objects in individual fragments is fine,
  but the group ID, object ID and offset could be inferred from the previous fragments.
  The message could be simplified by carrying just two flags, "is_last_fragment" and
  "is_fist_of_group". A Start Point message could be inserted at the beginning of the
  stream to indicate the initial value of group ID and object ID. Doing that would remove
  6 to 8 bytes of overhead per message.
NOTE: should update this format to carry a "flag".

### Fin Message

The Fragment message indicates the final point of a media stream. 

```
 * quicrq_fin_message {
 *     message_type(i),
 *     final_group_id(i),
 *     final_object_id(i)
 * }
```

The message type will be set to FIN (3). The final `group_id` is set to the `group_id`
of the last fragment sent. The final `object_id` is set to the object_id of the last
fragment sent, plus 1. This message is not sent when fragments are sent on stream.

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
 * quicrq_datagram_header { 
 *     datagram_stream_id (i)
 *     group_id (i)
 *     object_id (i)
 *     offset_and_fin (i)
 *     queue_delay (i)
 *     flags (8)
 *     [nb_objects_previous_group (i)]
 * }
```

The datagram_stream_id identifies a specific media stream. The ID is chosen by the receiver of the media stream,
and conveyed by the Request or Accept messages.

The `offset_and_fin` field encodes two values, as in:
```
offset_and_fin = 2*offset + is_last_fragment
```

The `flags` field is reserved, but currently set to 0 by senders and ignored by receivers.

The `nb_objects_previous_group` is present if and only if this is the first fragment of the first object
in a group, i.e., `object_id` and `offset` are both zero. The number indicates how many objects were sent
in the previous groups. It enables receiver to check whether all these objects have been received.

### Datagram Repeats

The prototype uses a feature of Picoquic to determine whether a previously sent datagram is probably
lost, and in that case repeats the fragment. The Picoquic feature is susceptible of "false positive"
errors, which means that the same fragment could very well be received multiple times.

Relays may forward fragments even if they arrive out of order.


