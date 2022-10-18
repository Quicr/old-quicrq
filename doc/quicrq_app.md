# QUICRQ proof of concept application (quicrq_app)

The application `quicrq_app` was developed to demonstrate the QUICR
protocol, as implemented in the QUICRQ project. The app can function
in three mode:

* as a relay, implementing the full functionality of a QUICRQ relay in
a single process.
* as a limited origin server, implementing sufficient functionality to
function as origin for a variety of client published media
* as a test client, only capable at this point of publishing and consuming
test media in the same format used by unit tests.

Command line parameters are used to specialize the application in the relay,
origin or client role, to provide necessary parameters to the underlying
Quic stack, to specify the test scenarios in the test client, and to
chose between execution options in the test protocol.

## List of options

Running `quicrq_app -h` (or `quicrq_app.exe -h` on Windows) produces the
following list of options:

```
Usage: quicrq_app <options> [mode] [server_name ['d'|'s'] port [scenario]]
  mode can be one of client, relay or server.
  For the client and relay mode, specify server_name and port,
  and either 'd' or 's' for datagram or stream mode.
  For the server and relay mode, use -p to specify the port,
  and also -c and -k for certificate and matching private key.
Picoquic options:
  -c file         cert file
  -k file         key file
  -K file         ESNI private key file (default: don't use ESNI)
  -p number       server port
  -v              Version proposed by client, e.g. -v ff000012
  -o folder       Folder where client writes downloaded files, defaults to current directory.
  -w folder       Folder containing web pages served by server
  -x number       Maximum number of concurrent connections, default 256
  -r              Do Retry Request
  -R option       Randomize packet number spaces: none(0), initial(1, default), all(2).
  -s <64b 64b>    Reset seed
  -X              Disable the check for blocked ports
  -S folder       Set the path to the source files to find the default files
  -G cc_algorithm Use the specified congestion control algorithm: reno, cubic, bbr or fast. Defaults to bbr.
  -P number       Set the default spinbit policy
  -O number       Set the default lossbit policy
  -M number       Multipath option: none(0), full(1), simple(2), both(3)
  -e if           Send on interface (default: -1)
  -C cipher_suite_id specify cipher suite (e.g. -C 20 = chacha20)
  -E file         ESNI RR file (default: don't use ESNI)
  -i per-text-lb-spec See documentation for LB compatible CID configuration
  -l file         Log file, Log to stdout if file = "-". No text logging if absent.
  -L              Log all packets. If absent, log stops after 100 packets.
  -b folder       Binary logging to this directory. No binary logging if absent.
  -q folder       Qlog logging to this directory. No qlog logging if absent, but qlogs could be produced using picolog if binary logs are available.
  -m mtu_max      Largest mtu value that can be tried for discovery.
  -n sni          sni (default: server name)
  -a alpn         alpn (default function of version)
  -t file         root trust file
  -z              Set TLS zero share behavior on client, to force HRR
  -I length       Length of CNX_ID used by the client, default=8
  -D              no disk: do not save received files on disk
  -Q              send a large client hello in order to test post quantum readiness
  -T file         File storing the session tickets
  -N file         File storing the new tokens
  -B number       Set buffer size with SO_SNDBUF SO_RCVBUF
  -F file         Append performance reports to performance log
  -V              enable preemptive repeat
  -U              Version upgrade if server agrees, e.g. -U FF020000
  -0              Do not use UDP GSO or equivalent
  -j number       use bdp extension frame(1) or don't (0). Default=0
  -h This help message (null)

On the client, the scenario argument specifies the media files
that should be retrieved (get) or published (post):
  *{{'get'|'post'}':'<url>':'<path>[':'<log_path>]';'}
where:
  <url>:      The name by which the media is known
  <path>:     The local file where to store (get) or read (post) the media.)
  <log_path>: The local file where to write statistics (get only).)
```

## Running as origin

Running the test program in origin mode requires the following parameters:
```
quicrq_app -p <port> -c <certificate PEM file> -k <key PEM file> server
```
The keyword `server` at the end of the argument list specifies the
origin server mode.

To run as a Quic server, the program needs access to a server certificate
and the corresponding private key. In a real deployment, these would be
X509 certificates and the corresponding key. In a test deployment,
experimenters may use the test certificate and test key provided
with the code:

```
./certs/cert.pem
./certs/key.pem
```

The origin server will wait for QUIC connections from relays or clients
at the specified port.

## Running as relay

Running the program in relay mode requires the following parameters:

```
quicrq_app -p <port> -c <certificate PEM file> -k <key PEM file> relay <origin-ip> d <origin-port> 
```

The keyword `relay` at the end of the argument list specifies the
relay mode. It is followed by:

* origin-ip: the IP address of the server
* datagram or stream mode: the letters "d" or "s", specifying
  whether to exchange media with the origin in datagram or stream mode.
* origin-port: the port at which the origin server waits for connection

The certificate and key files have the same role as for the origin server.

## Running as client

Running the protocol as client requires the following parameters:

```
quicrq_app client <origin-or-relay-ip> d <origin-or-relay-port> <scenario>
```

The origin-or-relay-ip, datagram or stream mode and origin-or-relay-port determine where
the client will connect in the graph of relays and origin server. It may be directly to
an origin server, or to an intermediate relay. 

The scenario parameter determines the action taken by the client. Those may be
either posting a particular media stream, or requesting a media stream posted by 
another client. The syntax of the scenario parameter is expressed as:

```
*{{'get'|'post'}':'<url>':'<path>[':'<log_path>]';'}
```

Where:
 * the keyword `get` or `post` determine the nature of the action,
 * the `url` is a string of characters identifying the media stream across
   the entire set of clients, relays and origin server.
 * the `path` is the local file path where the media stream is stored (post
   action) or will be stored (get action)
 * the optional `log_path` will contain statistics per media frame.
   
Only one client should post a given URL. Attempt to post the same URL multiple time
are likely to create confusion in origin and relays, with unpredictable results.

The media file should be in the format used by the unit tests.

