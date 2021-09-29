# quicrq

Exploring QUIC Realtime transports as part of the QUICR project. The prototypes are developed using
[picoquic](https://github.com/private-octopus/picoquic)

At this stage, the code only runs on a simulation, as part of the test suite.

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
