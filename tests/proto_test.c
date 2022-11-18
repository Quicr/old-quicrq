/* Tests of message coding and decoding */
#include <stdlib.h>
#include <string.h>
#include "quicrq.h"
#include "quicrq_internal.h"
#include "quicrq_tests.h"
#include "quicrq_test_internal.h"
#include "picoquic_utils.h"

/*
#define QUICRQ_ACTION_REQUEST_STREAM 1
#define QUICRQ_ACTION_REQUEST_DATAGRAM 2
#define QUICRQ_ACTION_FIN_DATAGRAM 3
#define QUICRQ_ACTION_REQUEST_REPAIR 4
*/

#define URL1_BYTES 'e', 'x', 'a', 'm', 'p', 'l', 'e', '.', 'c', 'o', 'm', '/', 'm', 'e', 'd', 'i', 'a'

static uint8_t url1[] = { URL1_BYTES };

static quicrq_message_t stream_rq = {
    QUICRQ_ACTION_REQUEST_STREAM,
    sizeof(url1),
    url1,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    NULL,
    0,
    0,
    quicrq_subscribe_intent_current_group
};

static uint8_t stream_rq_bytes[] = {
    QUICRQ_ACTION_REQUEST_STREAM,
    sizeof(url1),
    URL1_BYTES,
    0x00
};

static quicrq_message_t datagram_rq = {
    QUICRQ_ACTION_REQUEST_DATAGRAM,
    sizeof(url1),
    url1,
    1234,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    NULL,
    0,
    0,
    quicrq_subscribe_intent_current_group
};

static uint8_t datagram_rq_bytes[] = {
    QUICRQ_ACTION_REQUEST_DATAGRAM,
    sizeof(url1),
    URL1_BYTES,
    0x00,
    0x44, 0xd2
};

static quicrq_message_t datagram_rq_next_group = {
    QUICRQ_ACTION_REQUEST_DATAGRAM,
    sizeof(url1),
    url1,
    1234,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    NULL,
    0,
    0,
    quicrq_subscribe_intent_next_group
};

static uint8_t datagram_rq_next_group_bytes[] = {
    QUICRQ_ACTION_REQUEST_DATAGRAM,
    sizeof(url1),
    URL1_BYTES,
    0x01,
    0x44, 0xd2
};

static quicrq_message_t datagram_rq_start_point = {
    QUICRQ_ACTION_REQUEST_DATAGRAM,
    sizeof(url1),
    url1,
    1234,
    4,
    9,
    0,
    0,
    0,
    0,
    0,
    NULL,
    0,
    0,
    quicrq_subscribe_intent_start_point
};

static uint8_t datagram_rq_start_point_bytes[] = {
    QUICRQ_ACTION_REQUEST_DATAGRAM,
    sizeof(url1),
    URL1_BYTES,
    0x02,
    0x04,
    0x09,
    0x44, 0xd2
};

static quicrq_message_t fin_msg = {
    QUICRQ_ACTION_FIN_DATAGRAM,
    0,
    NULL,
    0,
    17,
    123456,
    0,
    0,
    0,
    0,
    0,
    NULL,
    0,
    0,
    quicrq_subscribe_intent_current_group
};

static uint8_t fin_msg_bytes[] = {
    QUICRQ_ACTION_FIN_DATAGRAM,
    0x11,
    0x80, 0x01, 0xe2, 0x40
};

#define REPAIR_BYTES 1,2,3,4,5,6,7,8,9,10,11,12,13
static uint8_t repair_bytes[] = { REPAIR_BYTES };
static quicrq_message_t repair_request_msg = {
    QUICRQ_ACTION_REQUEST_REPAIR,
    0,
    NULL,
    0,
    0,
    123456,
    0,
    1234,
    0,
    1,
    sizeof(repair_bytes),
    NULL,
    0,
    0,
    quicrq_subscribe_intent_current_group
};

static uint8_t repair_request_msg_bytes[] = {
    QUICRQ_ACTION_REQUEST_REPAIR,
    0x0, 0x80, 0x01, 0xe2, 0x40,
    0x49, 0xa5,
    (uint8_t)sizeof(repair_bytes)
};

static quicrq_message_t fragment_msg = {
    QUICRQ_ACTION_FRAGMENT,
    0,
    NULL,
    0,
    0,
    123456,
    0,
    1234,
    0x17,
    1,
    sizeof(repair_bytes),
    repair_bytes,
    0,
    0,
    quicrq_subscribe_intent_current_group
};

static uint8_t fragment_msg_bytes[] = {
    QUICRQ_ACTION_FRAGMENT,
    0x00,
    0x80, 0x01, 0xe2, 0x40,
    0x49, 0xa5,
    0x17,
    (uint8_t)sizeof(repair_bytes),
    REPAIR_BYTES
};

static quicrq_message_t fragment_msg2 = {
    QUICRQ_ACTION_FRAGMENT,
    0,
    NULL,
    0,
    11,
    0,
    60,
    0,
    0x17,
    1,
    sizeof(repair_bytes),
    repair_bytes,
    0,
    0,
    quicrq_subscribe_intent_current_group
};

static uint8_t fragment_msg2_bytes[] = {
    QUICRQ_ACTION_FRAGMENT,
    0x0b,
    0x00,
    0x01,
    0x17,
    0x3c,
    (uint8_t)sizeof(repair_bytes),
    REPAIR_BYTES
};

static quicrq_message_t post_msg = {
    QUICRQ_ACTION_POST,
    sizeof(url1),
    url1,
    0,
    1,
    12,
    0,
    0,
    0,
    0,
    0,
    NULL,
    3,
    1,
    quicrq_subscribe_intent_current_group
};

static uint8_t post_msg_bytes[] = {
    QUICRQ_ACTION_POST,
    sizeof(url1),
    URL1_BYTES,
    3,
    1,
    1,
    12
};

static quicrq_message_t accept_dg = {
    QUICRQ_ACTION_ACCEPT,
    0,
    NULL,
    17,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    NULL,
    quicrq_transport_mode_datagram,
    0,
    quicrq_subscribe_intent_current_group
};

static uint8_t accept_dg_bytes[] = {
    QUICRQ_ACTION_ACCEPT,
    quicrq_transport_mode_datagram,
    17
};


static quicrq_message_t accept_st = {
    QUICRQ_ACTION_ACCEPT,
    0,
    NULL,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    NULL,
    quicrq_transport_mode_single_stream,
    0,
    quicrq_subscribe_intent_current_group
};

static uint8_t accept_st_bytes[] = {
    QUICRQ_ACTION_ACCEPT,
    quicrq_transport_mode_single_stream
};

static quicrq_message_t start_msg = {
    QUICRQ_ACTION_START_POINT,
    0,
    NULL,
    0,
    2469,
    123456,
    0,
    0,
    0,
    0,
    0,
    NULL,
    0,
    0,
    quicrq_subscribe_intent_current_group
};

static uint8_t start_msg_bytes[] = {
    QUICRQ_ACTION_START_POINT,
    0x49, 0xa5,
    0x80, 0x01, 0xe2, 0x40
};

static quicrq_message_t subscribe_msg = {
    QUICRQ_ACTION_SUBSCRIBE,
    sizeof(url1),
    url1,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    NULL,
    0,
    0,
    quicrq_subscribe_intent_current_group
};

static uint8_t subscribe_msg_bytes[] = {
    QUICRQ_ACTION_SUBSCRIBE,
    sizeof(url1),
    URL1_BYTES
};

static quicrq_message_t notify_msg = {
    QUICRQ_ACTION_NOTIFY,
    sizeof(url1),
    url1,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    NULL,
    0,
    0,
    quicrq_subscribe_intent_current_group
};

static uint8_t notify_msg_bytes[] = {
    QUICRQ_ACTION_NOTIFY,
    sizeof(url1),
    URL1_BYTES
};


static quicrq_message_t cache_policy_msg = {
    QUICRQ_ACTION_CACHE_POLICY,
    0,
    NULL,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    NULL,
    0,
    1,
    quicrq_subscribe_intent_current_group
};

static uint8_t cache_policy_bytes[] = {
    QUICRQ_ACTION_CACHE_POLICY,
    1
};



typedef struct st_proto_test_case_t {
    uint8_t* const data;
    size_t data_length;
    quicrq_message_t*  result;
} proto_test_case_t;

#define PROTO_TEST_ITEM(case_name, case_bytes) { case_bytes, sizeof(case_bytes), &case_name }
static proto_test_case_t proto_cases[] = {
    PROTO_TEST_ITEM(stream_rq, stream_rq_bytes),
    PROTO_TEST_ITEM(datagram_rq, datagram_rq_bytes),
    PROTO_TEST_ITEM(datagram_rq_next_group, datagram_rq_next_group_bytes),
    PROTO_TEST_ITEM(datagram_rq_start_point, datagram_rq_start_point_bytes),
    PROTO_TEST_ITEM(fin_msg, fin_msg_bytes),
    PROTO_TEST_ITEM(repair_request_msg, repair_request_msg_bytes),
    PROTO_TEST_ITEM(fragment_msg, fragment_msg_bytes),
    PROTO_TEST_ITEM(fragment_msg2, fragment_msg2_bytes),
    PROTO_TEST_ITEM(post_msg, post_msg_bytes),
    PROTO_TEST_ITEM(accept_dg, accept_dg_bytes),
    PROTO_TEST_ITEM(accept_st, accept_st_bytes),
    PROTO_TEST_ITEM(start_msg, start_msg_bytes),
    PROTO_TEST_ITEM(subscribe_msg, subscribe_msg_bytes),
    PROTO_TEST_ITEM(notify_msg, notify_msg_bytes),
    PROTO_TEST_ITEM(cache_policy_msg, cache_policy_bytes)
};

static uint8_t bad_bytes1[] = {
    0xcf, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    sizeof(url1),
    URL1_BYTES,
    0x00
};

static uint8_t bad_bytes2[] = {
    QUICRQ_ACTION_REQUEST_STREAM,
    0xcf, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    URL1_BYTES,
    0x00
};

static uint8_t bad_bytes3[] = {
    QUICRQ_ACTION_REQUEST_STREAM,
    0x8f, 0xff, 0xff, 0xff,
    URL1_BYTES,
    0x00
};

static uint8_t bad_bytes4[] = {
    QUICRQ_ACTION_REQUEST_STREAM,
    0x4f, 0xff,
    URL1_BYTES,
    0x00
};

static uint8_t bad_bytes5[] = {
    QUICRQ_ACTION_REQUEST_STREAM,
    sizeof(url1) + 1,
    URL1_BYTES,
    0x00
};

static uint8_t bad_bytes6[] = {
    QUICRQ_ACTION_REQUEST_DATAGRAM,
    0xcf, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    URL1_BYTES,
    0x00,
    0x44, 0xd2
};

static uint8_t bad_bytes7[] = {
    QUICRQ_ACTION_REQUEST_DATAGRAM,
    0x8f, 0xff, 0xff, 0xff,
    URL1_BYTES,
    0x00,
    0x44, 0xd2
};

static uint8_t bad_bytes8[] = {
    QUICRQ_ACTION_REQUEST_DATAGRAM,
    0x4f, 0xff,
    URL1_BYTES,
    0x44, 0xd2
};

static uint8_t bad_bytes9[] = {
    QUICRQ_ACTION_REQUEST_DATAGRAM,
    sizeof(url1) + 1,
    URL1_BYTES,
    0x44, 0xd2
};

static uint8_t bad_bytes10[] = {
    QUICRQ_ACTION_POST,
    sizeof(url1),
    URL1_BYTES,
    17
};

static uint8_t bad_bytes11[] = {
    QUICRQ_ACTION_POST,
    0x4F,
    0xFF,
    URL1_BYTES,
    17,
    1,
    12
};

static uint8_t bad_bytes12[] = {
    QUICRQ_ACTION_ACCEPT,
    17,
    17
};

static uint8_t bad_bytes13[] = {
    QUICRQ_ACTION_ACCEPT,
    quicrq_transport_mode_datagram
};

static uint8_t bad_bytes14[] = {
    QUICRQ_ACTION_START_POINT,
    0xFF, 0xa5,
    0x80, 0x01, 0xe2, 0x40
};

static uint8_t bad_bytes15[] = {
    QUICRQ_ACTION_FRAGMENT,
    0x0b,
    0x00,
    0x01,
    0x17,
    0x02,
    0xff, 0xff
};

static uint8_t bad_bytes16[] = {
    QUICRQ_ACTION_REQUEST_DATAGRAM,
    sizeof(url1),
    URL1_BYTES,
    0x44, 0xd2
};

static uint8_t bad_bytes17[] = {
    QUICRQ_ACTION_REQUEST_DATAGRAM,
    sizeof(url1),
    URL1_BYTES,
    0x02,
    0x44, 0xd2
};

static uint8_t bad_bytes18[] = {
    QUICRQ_ACTION_REQUEST_DATAGRAM,
    sizeof(url1),
    URL1_BYTES,
    0x02,
    0x04,
    0x44, 0xd2
};

static uint8_t bad_bytes19[] = {
    QUICRQ_ACTION_REQUEST_DATAGRAM,
    sizeof(url1),
    URL1_BYTES,
    0x03,
    0x44, 0xd2
};

static uint8_t bad_bytes20[] = {
    QUICRQ_ACTION_REQUEST_STREAM,
    sizeof(url1),
    URL1_BYTES
};

static uint8_t bad_bytes21[] = {
    QUICRQ_ACTION_REQUEST_STREAM,
    sizeof(url1),
    URL1_BYTES,
    0x03
};

typedef struct st_proto_test_bad_case_t {
    uint8_t* const data;
    size_t data_length;
} proto_test_bad_case_t;

proto_test_bad_case_t bad_test = { bad_bytes1, sizeof(bad_bytes1) };

#define PROTO_TEST_BAD_ITEM(case_bytes) { case_bytes, sizeof(case_bytes) }

static proto_test_bad_case_t proto_bad_cases[] = {
    PROTO_TEST_BAD_ITEM(bad_bytes1),
    PROTO_TEST_BAD_ITEM(bad_bytes2),
    PROTO_TEST_BAD_ITEM(bad_bytes3),
    PROTO_TEST_BAD_ITEM(bad_bytes4),
    PROTO_TEST_BAD_ITEM(bad_bytes5),
    PROTO_TEST_BAD_ITEM(bad_bytes6),
    PROTO_TEST_BAD_ITEM(bad_bytes7),
    PROTO_TEST_BAD_ITEM(bad_bytes8),
    PROTO_TEST_BAD_ITEM(bad_bytes9),
    PROTO_TEST_BAD_ITEM(bad_bytes10),
    PROTO_TEST_BAD_ITEM(bad_bytes11),
    PROTO_TEST_BAD_ITEM(bad_bytes12),
    PROTO_TEST_BAD_ITEM(bad_bytes13),
    PROTO_TEST_BAD_ITEM(bad_bytes14),
    PROTO_TEST_BAD_ITEM(bad_bytes15),
    PROTO_TEST_BAD_ITEM(bad_bytes16),
    PROTO_TEST_BAD_ITEM(bad_bytes17),
    PROTO_TEST_BAD_ITEM(bad_bytes18),
    PROTO_TEST_BAD_ITEM(bad_bytes19),
    PROTO_TEST_BAD_ITEM(bad_bytes20),
    PROTO_TEST_BAD_ITEM(bad_bytes21)
};

int proto_msg_test()
{
    int ret = 0;

    /* Decoding tests */
    for (size_t i = 0; ret == 0 && i < sizeof(proto_cases) / sizeof(proto_test_case_t); i++) {
        const uint8_t * bytes = proto_cases[i].data;
        const uint8_t * bytes_max = bytes + proto_cases[i].data_length;
        quicrq_message_t result = { 0 };

        bytes = quicrq_msg_decode(bytes, bytes_max, &result);

        if (bytes == NULL) {
            ret = -1;
        }
        else if (bytes != bytes_max) {
            ret = -1;
        }
        else if (result.message_type != proto_cases[i].result->message_type) {
            ret = -1;
        }
        else if (result.url_length != proto_cases[i].result->url_length) {
            ret = -1;
        }
        else if (result.url_length != 0 && result.url == NULL) {
            ret = -1;
        }
        else if (result.url_length != 0 && memcmp(result.url, proto_cases[i].result->url, result.url_length) != 0) {
            ret = -1;
        }
        else if (result.media_id != proto_cases[i].result->media_id) {
            ret = -1;
        }
        else if (result.group_id != proto_cases[i].result->group_id) {
            ret = -1;
        }
        else if (result.object_id != proto_cases[i].result->object_id) {
            ret = -1;
        }
        else if (result.offset != proto_cases[i].result->offset) {
            ret = -1;
        }
        else if (result.is_last_fragment != proto_cases[i].result->is_last_fragment) {
            ret = -1;
        }
        else if (result.subscribe_intent != proto_cases[i].result->subscribe_intent) {
            ret = -1;
        }
        else if (result.length != proto_cases[i].result->length) {
            ret = -1;
        }
    }

    /* Encoding tests */
    for (size_t i = 0; ret == 0 && i < sizeof(proto_cases) / sizeof(proto_test_case_t); i++) {
        uint8_t msg[512];
        uint8_t* bytes = quicrq_msg_encode(msg, msg + sizeof(msg), proto_cases[i].result);

        if (bytes == NULL) {
            ret = -1;
        }
        else if ((size_t)(bytes - msg) != proto_cases[i].data_length) {
            ret = -1;
        }
        else if (memcmp(msg, proto_cases[i].data, proto_cases[i].data_length) != 0) {
            ret = -1;
        }
    }

    /* Bad length tests */
    for (size_t i = 0; ret == 0 && i < sizeof(proto_cases) / sizeof(proto_test_case_t); i++) {
        for (size_t l = 0; l < proto_cases[i].data_length; l++) {
            const uint8_t* bytes = proto_cases[i].data;
            const uint8_t* bytes_max = bytes + l;
            quicrq_message_t result = { 0 };

            bytes = quicrq_msg_decode(bytes, bytes_max, &result);
            if (bytes != NULL) {
                ret = -1;
            }
        }
    }

    /* Bad data tests */
    for (size_t i = 0; ret == 0 && i < sizeof(proto_bad_cases) / sizeof(proto_test_bad_case_t); i++) {
        const uint8_t* bytes = proto_bad_cases[i].data;
        const uint8_t* bytes_max = bytes + proto_bad_cases[i].data_length;
        quicrq_message_t result = { 0 };

        bytes = quicrq_msg_decode(bytes, bytes_max, &result);
        if (bytes != NULL) {
            ret = -1;
        }
    }

    return ret;
}
