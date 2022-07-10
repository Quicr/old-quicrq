# Architecture of the QUICRQ Relay

The QUICRQ prototype implements the QUICR relay functions. The relays are capable of
receiving data in stream mode or in datagram mode. In both modes, relays will cache
fragments as they arrive. 

```
   +------------+
   | Incoming   |
-->| Connection |            +------------+
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
         +---->| Connection |-->   |
         |     +------------+      |
         |                      retrieve
         |                      fragments
        ...                        |
         |                   +-----+------+
         |                   | Outgoing   |
         +------------------>| Connection |-->
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

There is a cache instance for each media URL. The cahe properties 
include the URL of the media, the identification of the first
available fragment, and the indication of the final fragment if it
has been received. 

TTL management will periodically purge the cache records that are too
old, or the cache instances that are not needed anymore.