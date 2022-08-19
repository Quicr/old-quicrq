cmake_minimum_required(VERSION 3.13)

# Building tests by default depends on whether this is a subproject
if(DEFINED PROJECT_NAME)
    option(quicrq_BUILD_TESTS "Build Tests for quicrq" OFF)
else()
    option(quicrq_BUILD_TESTS "Build Tests for quicrq" ON)
endif()

project(quicrq
        VERSION 1.0.0.0
        DESCRIPTION "quicrq library"
        LANGUAGES C CXX)


find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)

# Bring in dependencies
add_subdirectory(dependencies)

find_package(OpenSSL REQUIRED)
message(STATUS "root: ${OPENSSL_ROOT_DIR}")
message(STATUS "OpenSSL_VERSION: ${OPENSSL_VERSION}")
message(STATUS "OpenSSL_INCLUDE_DIR: ${OPENSSL_INCLUDE_DIR}")
message(STATUS "OpenSSL_LIBRARIES: ${OPENSSL_LIBRARIES}")


add_library(quicrq-core
    lib/fragment.c
    lib/quicrq.c
    lib/proto.c
    lib/reassembly.c
    lib/relay.c
    lib/object_consumer.c
    lib/object_source.c
)
target_link_libraries(quicrq-core picoquic-core)
target_include_directories(quicrq-core PUBLIC include)
set_target_properties(quicrq-core
    PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS YES)
target_compile_options(quicrq-core PRIVATE
    $<$<OR:$<C_COMPILER_ID:Clang>,$<C_COMPILER_ID:AppleClang>,$<C_COMPILER_ID:GNU>>: -Wpedantic -Wextra -Wall>
    $<$<C_COMPILER_ID:MSVC>: >)


add_library(quicrq-tests
    tests/basic_test.c
    tests/congestion_test.c
    tests/fourlegs_test.c
    tests/fragment_test.c
    tests/proto_test.c
    tests/pyramid_test.c
    tests/relay_test.c
    tests/subscribe_test.c
    tests/test_media.c
    tests/threelegs_test.c
    tests/triangle_test.c
    tests/twomedia_test.c
    tests/twoways_test.c
)
target_include_directories(quicrq-tests
    PUBLIC
        include
    PRIVATE
        lib tests
)
target_link_libraries(quicrq-tests picoquic-core picoquic-log)
set_target_properties(quicrq-tests
    PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS YES)
target_compile_options(quicrq-tests PRIVATE
    $<$<OR:$<C_COMPILER_ID:Clang>,$<C_COMPILER_ID:AppleClang>,$<C_COMPILER_ID:GNU>>: -Wpedantic -Wextra -Wall>
    $<$<C_COMPILER_ID:MSVC>: >)


add_executable(quicrq_app src/quicrq_app.c)
target_include_directories(quicrq_app
    PUBLIC
        include
    PRIVATE
        tests
)
target_link_libraries(quicrq_app
    picoquic-core
    quicrq-tests
    quicrq-core
    Threads::Threads
)
set_target_properties(quicrq_app
    PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS YES)
target_compile_options(quicrq_app PRIVATE
    $<$<OR:$<C_COMPILER_ID:Clang>,$<C_COMPILER_ID:AppleClang>,$<C_COMPILER_ID:GNU>>: -Wpedantic -Wextra -Wall>
    $<$<C_COMPILER_ID:MSVC>: >)


include(CTest)

if(BUILD_TESTING AND quicrq_BUILD_TESTS)

    add_executable(quicrq_t src/quicrq_t.c)
    target_include_directories(quicrq_t
        PUBLIC
            include
        PRIVATE
            tests)
    target_link_libraries(quicrq_t
        picoquic-core
        picoquic-log
        quicrq-tests
        quicrq-core
        Threads::Threads)
    set_target_properties(quicrq_t
        PROPERTIES
            C_STANDARD 11
            C_STANDARD_REQUIRED YES
            C_EXTENSIONS YES)
    target_compile_options(quicrq_t PRIVATE
        $<$<OR:$<C_COMPILER_ID:Clang>,$<C_COMPILER_ID:AppleClang>,$<C_COMPILER_ID:GNU>>: -Wpedantic -Wextra -Wall>
        $<$<C_COMPILER_ID:MSVC>: >)

    add_test(NAME quicrq_t
             COMMAND quicrq_t -S ${PROJECT_SOURCE_DIR})

endif()


#
# Adds clangformat as target that formats all source files
#
# get all project files for formatting
file(GLOB_RECURSE CLANG_FORMAT_SOURCE_FILES *.c *.h)

add_custom_target(
    quicrq_clangformat
    COMMAND clang-format
    -style=Webkit
    -i
    ${CLANG_FORMAT_SOURCE_FILES}
)
