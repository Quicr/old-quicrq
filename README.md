# quicrq

Exploring QUIC Realtime transports as part of the QUICR project. The QUICR project explores a new
media transport architecture in which both realtime and streaming media can be delivered over
a "realtime content delivery network", in which clients access or post media segments through relays,
and relays get the data from other relays or from an origin server. Relays manage caches of media
segment.

The prototypes are developed using
[picoquic](https://github.com/private-octopus/picoquic)

The project builds:

* a library implementing the `quicrq` protocol,
* a test tool, `quicrq_t`, for running unit tests and verifying ports,
* a demo application, `quicrq_app`, for testing the protocol over real networks.

The demo application implements the server, client and relay functions of the protocol.
Server and clients can publish simulated media segments, using the same "simulated media files" format
as the test library. The relay does not make assumptions on the type of media files.
The server is a very simplified version of the "origin server" implemented in the architecture.
The demo application has multiple options, which can be listed by calling `quicrq_app -h`.

## Installing on Linux 

To build on a Unix machine, you need to install first [picotls](https://github.com/h2o/picotls/) and [picoquic](https://github.com/private-octopus/picoquic).
```
git clone https://github.com/quicr/quicrq/
cd quicrq
CMake .
make
```
To run the tests, assuming that picoquic, picotls and quicrq have been downloaded in the same directory:
```
./quicrq_t
```
If the installation is in a different directory, you will need to specify:
```
quicrq_t -S <path to quicrq sources> -P <path to picoquic sources>
```

## Installing on Windows

To install on a Windows machine, after cloning the project, you will find a Visual Studio solution at:
```
(cloning directory)\quicrq\quicrq_vs.sln
```
That solution can build the test application `quicrq_t.exe`. Tests are expected to run in the
output directory, e.g.:
```
(cloning directory)\quicrq\x64\debug\
```

## Developing

Development follows the test driven development approach. When adding new features, please add a
test for that feature in the test suite, typically by adding code in the `tests` folder.
You will also need to update:

* the list of tests in the program `quicrq_t.c`
* the list of visual studio unit tests in `UnitTest/UnitTest.cpp`

Proposed code changes should be packaged in push requests. The project uses github actions to
run tests after each check in. Push requests should only be checked in if all the tests
are passing, and if the code has been reviewed.
