#include <stdlib.h>
#include <string.h>
#include "quicrq.h"
#include "quicrq_internal.h"
#include "quicrq_tests.h"
#include "quicrq_test_internal.h"
#include "picoquic.h"
#include "picoquic_utils.h"

int picoquic_set_binlog(picoquic_quic_t* quic, char const* binlog_dir);
int picoquic_set_textlog(picoquic_quic_t* quic, char const* textlog_file);

#ifdef _WINDOWS
#ifdef _WINDOWS64
#define QUICRQ_PICOQUIC_DEFAULT_SOLUTION_DIR "..\\..\\..\\picoquic\\"
#define QUICRQ_DEFAULT_SOLUTION_DIR "..\\..\\"
#else
#define QUICRQ_PICOQUIC_DEFAULT_SOLUTION_DIR "..\\..\\picoquic\\"
#define QUICRQ_DEFAULT_SOLUTION_DIR "..\\"
#endif
#else
#define QUICRQ_PICOQUIC_DEFAULT_SOLUTION_DIR "../picoquic/"
#define QUICRQ_DEFAULT_SOLUTION_DIR "./"
#endif

char const* quicrq_test_picoquic_solution_dir = QUICRQ_PICOQUIC_DEFAULT_SOLUTION_DIR;
char const* quicrq_test_solution_dir = QUICRQ_DEFAULT_SOLUTION_DIR;


/* Test configuration: nodes, sources, addresses and links.
 * Each source is connected to a node, identified by a node_id
 * Nodes are connected via one-way links, identified by a link_id.
 * The links can be either symmetric or asymmetric. The link context
 * includes for each link the "return link" -- either itself or
 * a different link.
 * Each link can deliver data to a set of nodes. The relation between
 * the link and the nodes is an "attachment", which identifies the
 * link and a node, plus the IP address at which the node can receive data.
 * When a packet arrives on a node, the packet's IP address is used to
 * find which node shall receive it.
 * When a packet is posted on a link, the source IP address is posted
 * to the IP address of the attachment between that link and the return link.
 */

typedef struct st_quicrq_test_attach_t {
    int node_id;
    int link_id;
    struct sockaddr_storage node_addr;
} quicrq_test_attach_t;

typedef struct st_quicrq_test_source_t {
    /* TODO: relation between source and node */
    uint64_t next_source_time;
    quicrq_media_source_ctx_t* srce_ctx;
} quicrq_test_source_t;

typedef struct st_quicrq_test_config_t {
    uint64_t simulated_time;
    char test_server_cert_file[512];
    char test_server_key_file[512];
    char test_server_cert_store_file[512];
    uint8_t ticket_encryption_key[16];
    int nb_nodes;
    quicrq_ctx_t** nodes;
    int nb_links;
    picoquictest_sim_link_t** links;
    int* return_links;
    int nb_attachments;
    quicrq_test_attach_t* attachments;
    int nb_sources;
    quicrq_test_source_t* sources;
} quicrq_test_config_t;

/* Find arrival context by link ID and destination address */
int quicrq_test_find_dest_node(quicrq_test_config_t* config, int link_id, struct sockaddr* addr)
{
    int node_id = -1;

    for (int d_attach = 0; d_attach < config->nb_attachments; d_attach++) {
        if (config->attachments[d_attach].link_id == link_id &&
            picoquic_compare_addr((struct sockaddr*)&config->attachments[d_attach].node_addr, addr) == 0) {
            node_id = config->attachments[d_attach].node_id;
            break;
        }
    }
    return (node_id);
}

/* Find departure link by destination address.
 * The code verifies that the return link is present.
 * If srce_addr is prsent and set to AF_UNSPEC, it is filled with appropriate address.
 */
int quicrq_test_find_send_link(quicrq_test_config_t* config, int srce_node_id, const struct sockaddr* dest_addr, struct sockaddr_storage* srce_addr)
{
    int dest_link_id = -1;

    for (int s_attach = 0; s_attach < config->nb_attachments && dest_link_id == -1; s_attach++) {
        if (config->attachments[s_attach].node_id == srce_node_id) {
            int link_id = config->return_links[config->attachments[s_attach].link_id];
            for (int d_attach = 0; d_attach < config->nb_attachments; d_attach++) {
                if (config->attachments[d_attach].link_id == link_id &&
                    picoquic_compare_addr((struct sockaddr*)&config->attachments[d_attach].node_addr, dest_addr) == 0) {
                    if (srce_addr != NULL && srce_addr->ss_family == AF_UNSPEC) {
                        picoquic_store_addr(srce_addr, (struct sockaddr*)&config->attachments[s_attach].node_addr);
                    }
                    dest_link_id = config->attachments[d_attach].link_id;
                    break;
                }
            }
        }
    }

    return dest_link_id;
}

/* Find destination address from source and destination node id. */
struct sockaddr* quicrq_test_find_send_addr(quicrq_test_config_t* config, int srce_node_id, int dest_node_id)
{
    struct sockaddr* dest_addr = NULL;
    for (int s_attach = 0; s_attach < config->nb_attachments && dest_addr == NULL; s_attach++) {
        if (config->attachments[s_attach].node_id == srce_node_id) {
            int link_id = config->return_links[config->attachments[s_attach].link_id];
            for (int d_attach = 0; d_attach < config->nb_attachments; d_attach++) {
                if (config->attachments[d_attach].link_id == link_id &&
                    config->attachments[d_attach].node_id == dest_node_id){
                    dest_addr = (struct sockaddr*)&config->attachments[d_attach].node_addr;
                    break;
                }
            }
        }
    }

    return dest_addr;
}

/* Packet departure from selected node */
int quicrq_test_packet_departure(quicrq_test_config_t* config, int node_id, int* is_active)
{
    int ret = 0;
    picoquictest_sim_packet_t* packet = picoquictest_sim_link_create_packet();

    if (packet == NULL) {
        /* memory error during test. Something is really wrong. */
        ret = -1;
    }
    else {
        /* check whether there is something to send */
        int if_index = 0;

        ret = picoquic_prepare_next_packet(config->nodes[node_id]->quic, config->simulated_time,
            packet->bytes, PICOQUIC_MAX_PACKET_SIZE, &packet->length,
            &packet->addr_to, &packet->addr_from, &if_index, NULL, NULL);

        if (ret != 0)
        {
            /* useless test, but makes it easier to add a breakpoint under debugger */
            free(packet);
            ret = -1;
        }
        else if (packet->length > 0) {
            /* Find the exit link. This assumes destination addresses are available on only one link */
            int link_id = quicrq_test_find_send_link(config, node_id, (struct sockaddr*)&packet->addr_to, &packet->addr_from);

            if (link_id >= 0) {
                *is_active = 1;
                picoquictest_sim_link_submit(config->links[link_id], packet, config->simulated_time);
            }
            else {
                /* packet cannot be routed. */
                /* TODO: keep track! */
                free(packet);
            }
        }
    }

    return ret;
}

/* Process arrival of a packet from a link */
int quicrq_test_packet_arrival(quicrq_test_config_t* config, int link_id, int * is_active)
{
    int ret = 0;
    picoquictest_sim_packet_t* packet = picoquictest_sim_link_dequeue(config->links[link_id], config->simulated_time);

    if (packet == NULL) {
        /* unexpected, probably bug in test program */
        ret = -1;
    }
    else {
        int node_id = quicrq_test_find_dest_node(config, link_id, (struct sockaddr*)&packet->addr_to);

        if (node_id >= 0) {
            *is_active = 1;

            ret = picoquic_incoming_packet(config->nodes[node_id]->quic,
                packet->bytes, (uint32_t)packet->length,
                (struct sockaddr*)&packet->addr_from,
                (struct sockaddr*)&packet->addr_to, 0, 0,
                config->simulated_time);
        }
        else {
            /* TODO: keep track of failures */
        }
        free(packet);
    }

    return ret;
}

/* Execute the loop */
int quicrq_test_loop_step(quicrq_test_config_t* config, int* is_active)
{
    int ret = 0;
    int next_step_type = 0;
    int next_step_index = 0;
    uint64_t next_time = UINT64_MAX;

    /* TODO: insert source timing in the simulation */
    /* Check which source has the lowest time */
    for (int i = 0; i < config->nb_sources; i++) {
        if (config->sources[i].next_source_time < next_time) {
            next_time = config->sources[i].next_source_time;
            next_step_type = 1;
            next_step_index = i;
        }
    }

    /* Check which node has the lowest wait time */
    for (int i = 0; i < config->nb_nodes; i++) {
        uint64_t quic_time = picoquic_get_next_wake_time(config->nodes[i]->quic, config->simulated_time);
        if (quic_time < next_time) {
            next_time = quic_time;
            next_step_type = 2;
            next_step_index = i;
        }
    }
    /* Check which link has the lowest arrival time */
    for (int i = 0; i < config->nb_nodes; i++) {
        if (config->links[i]->first_packet != NULL &&
            config->links[i]->first_packet->arrival_time < next_time) {
            next_time = config->links[i]->first_packet->arrival_time;
            next_step_type = 3;
            next_step_index = i;
        }
    }
    if (next_time < UINT64_MAX) {
        /* Update the time */
        if (next_time > config->simulated_time) {
            config->simulated_time = next_time;
        }
        switch (next_step_type) {
        case 1: /* Media ready on source #next_step_index */
            quicrq_source_wakeup(config->sources[next_step_index].srce_ctx);
            config->sources[next_step_index].next_source_time = UINT64_MAX;
            break;
        case 2: /* Quicrq context #next_step_index is ready to send data */
            ret = quicrq_test_packet_departure(config, next_step_index, is_active);
            break;
        case 3:
            /* If arrival, take next packet, find destination by address, and submit to end-of-link context */
            ret = quicrq_test_packet_arrival(config, next_step_index, is_active);
            break;
        default:
            /* This should never happen! */
            ret = -1;
            break;
        }
    }
    else {
        ret = -1;
    }

    return ret;
}

/* Delete a configuration */
void quicrq_test_config_delete(quicrq_test_config_t* config)
{
    if (config->nodes != NULL) {
        for (int i = 0; i < config->nb_nodes; i++) {
            if (config->nodes[i] != NULL) {
                quicrq_delete(config->nodes[i]);
            }
        }
        free(config->nodes);
    }

    if (config->links != NULL) {
        for (int i = 0; i < config->nb_links; i++) {
            if (config->links[i] != NULL) {
                picoquictest_sim_link_delete(config->links[i]);
            }
        }
        free(config->links);
    }

    if (config->return_links != NULL) {
        free(config->return_links);
    }

    if (config->attachments != NULL) {
        free(config->attachments);
    }

    if (config->sources != NULL) {
        free(config->sources);
    }

    free(config);
}

/* Create a configuration */
quicrq_test_config_t* quicrq_test_config_create(int nb_nodes, int nb_links, int nb_attachments, int nb_sources)
{
    quicrq_test_config_t* config = (quicrq_test_config_t*)malloc(sizeof(quicrq_test_config_t));

    if (config != NULL) {
        int success = 1;

        memset(config, 0, sizeof(quicrq_test_config_t));
        memset(config->ticket_encryption_key, 0x55, sizeof(config->ticket_encryption_key));

        /* Locate the default cert, key and root in the Picoquic solution*/
        if (picoquic_get_input_path(config->test_server_cert_file, sizeof(config->test_server_cert_file),
            quicrq_test_picoquic_solution_dir, PICOQUIC_TEST_FILE_SERVER_CERT) != 0 ||
            picoquic_get_input_path(config->test_server_key_file, sizeof(config->test_server_key_file),
                quicrq_test_picoquic_solution_dir, PICOQUIC_TEST_FILE_SERVER_KEY) != 0 ||
            picoquic_get_input_path(config->test_server_cert_store_file, sizeof(config->test_server_cert_store_file),
                quicrq_test_picoquic_solution_dir, PICOQUIC_TEST_FILE_CERT_STORE) != 0){
            success = 0;
        }

        if (nb_nodes <= 0 || nb_nodes > 0xffff) {
            success = 0;
        }
        else if (success) {
            config->nodes = (quicrq_ctx_t**)malloc(nb_nodes * sizeof(quicrq_ctx_t*));
            success &= (config->nodes != NULL);
            if (success) {
                memset(config->nodes, 0, nb_nodes * sizeof(quicrq_ctx_t*));
                config->nb_nodes = nb_nodes;
            }
        }

        if (nb_links <= 0 || nb_links > 0xffff) {
            success = 0;
        } else if (success) {
            config->links = (picoquictest_sim_link_t**)malloc(nb_links * sizeof(picoquictest_sim_link_t*));
            config->return_links = (int*)malloc(nb_links * sizeof(int));
            success &= (config->links != NULL);

            if (success) {
                memset(config->links, 0, nb_links * sizeof(picoquictest_sim_link_t*));
                config->nb_links = nb_links;

                for (int i = 0; success && (i < nb_links); i++) {
                    picoquictest_sim_link_t* link = picoquictest_sim_link_create(0.01, 10000, NULL, 0, config->simulated_time);
                    config->links[i] = link;
                    success &= (link != NULL);
                }
            }
        }


        if (nb_attachments <= 0 || nb_attachments > 0xffff) {
            success = 0;
        } else if (success) {
            config->attachments = (quicrq_test_attach_t*)malloc(nb_attachments * sizeof(quicrq_test_attach_t));
            success &= (config->attachments != NULL);

            if (success) {
                memset(config->attachments, 0, nb_attachments * sizeof(quicrq_test_attach_t));
                config->nb_attachments = nb_attachments;
                for (int i = 0; success && (i < config->nb_attachments); i++) {
                    char addr_text[128];
                    quicrq_test_attach_t* p_attach = &config->attachments[i];

                    if (picoquic_sprintf(addr_text, sizeof(addr_text), NULL, "%x::%x", i + 0x1000, i + 0x1000) == 0) {
                        picoquic_store_text_addr(&p_attach->node_addr, addr_text, i + 0x1000);
                    }
                    else {
                        success = 0;
                    }
                }
            }
        }

        if (nb_sources <= 0 || nb_sources > 0xffff) {
            success = 0;
        }
        else if (success) {
            config->sources = (quicrq_test_source_t*)malloc(nb_sources * sizeof(quicrq_test_source_t));
            success &= (config->sources != NULL);

            if (success) {
                memset(config->sources, 0, nb_sources * sizeof(quicrq_test_source_t));
                config->nb_sources = nb_sources;
            }
        }

        if (!success) {
            quicrq_test_config_delete(config);
            config = NULL;
        }
    }

    return config;
}

quicrq_test_config_t* quicrq_test_basic_config_create()
{
    /* Create a configuration with just two nodes, two links, one source and two attachment points.*/
    quicrq_test_config_t* config = quicrq_test_config_create(2, 2, 2, 1);
    if (config != NULL) {
        /* Create the contexts for the origin and the client */
        config->nodes[0] = quicrq_create(QUICRQ_ALPN,
            config->test_server_cert_file, config->test_server_key_file, NULL, NULL, NULL,
            config->ticket_encryption_key, sizeof(config->ticket_encryption_key),
            &config->simulated_time);
        config->nodes[1] = quicrq_create(QUICRQ_ALPN,
            NULL, NULL, config->test_server_cert_store_file, NULL, NULL,
            NULL, 0, &config->simulated_time);
        config->sources[0].srce_ctx = NULL;
        if (config->nodes[0] == NULL || config->nodes[1] == NULL) {
            quicrq_test_config_delete(config);
            config = NULL;
        }
    }
    if (config != NULL) {
        /* Populate the attachments */
        config->return_links[0] = 1;
        config->attachments[0].link_id = 0;
        config->attachments[0].node_id = 0;
        /* TODO: initiate source */
        config->return_links[1] = 0;
        config->attachments[1].link_id = 1;
        config->attachments[1].node_id = 1;
    }
    return config;
}

quicrq_cnx_ctx_t* quicrq_test_basic_create_cnx(quicrq_test_config_t* config, int client_node, int server_node)
{
    quicrq_ctx_t* qr_ctx = config->nodes[client_node];
    picoquic_quic_t * quic = quicrq_get_quic_ctx(qr_ctx);
    picoquic_cnx_t* cnx = NULL;
    quicrq_cnx_ctx_t* cnx_ctx = NULL;
    struct sockaddr* addr_to = NULL;
    
    /* Find an attachment leading to server node */
    for (int i = 0; i < config->nb_attachments; i++) {
        addr_to = quicrq_test_find_send_addr(config, 1, 0);
    }

    if (addr_to != NULL) {
        cnx = picoquic_create_cnx(quic, picoquic_null_connection_id, picoquic_null_connection_id,
            addr_to, config->simulated_time, 0, NULL, QUICRQ_ALPN, 1);
        /* Set parameters */
        if (!server_node) {
            picoquic_tp_t client_parameters;

            quicrq_init_transport_parameters(&client_parameters, 1);
            picoquic_set_transport_parameters(cnx, &client_parameters);
        }

        if (picoquic_start_client_cnx(cnx) != 0){
            picoquic_delete_cnx(cnx);
            cnx = NULL;
        }
        if (cnx != NULL) {
            cnx_ctx = quicrq_create_cnx_context(qr_ctx, cnx);
        }
    }
    return cnx_ctx;
}

#ifdef _WINDOWS
#define QUICRQ_TEST_BASIC_SOURCE "tests\\video1_source.bin"
#else
#define QUICRQ_TEST_BASIC_SOURCE "tests/video1_source.bin"
#endif
#define QUICRQ_TEST_BASIC_RESULT "basic_result.bin"
#define QUICRQ_TEST_BASIC_LOG    "basic_log.csv"


/* Basic connection test */
int quicrq_basic_test_one(int is_real_time, int use_datagrams)
{
    int ret = 0;
    int nb_steps = 0;
    int nb_inactive = 0;
    int is_closed = 0;
    const uint64_t max_time = 360000000;
    const int max_inactive = 128;

    quicrq_test_config_t* config = quicrq_test_basic_config_create();
    quicrq_cnx_ctx_t* cnx_ctx = NULL;
    char media_source_path[512];

    if (config == NULL) {
        ret = -1;
    }

    /* Locate the source and reference file */
    if (picoquic_get_input_path(media_source_path, sizeof(media_source_path),
        quicrq_test_solution_dir, QUICRQ_TEST_BASIC_SOURCE) != 0) {
        ret = -1;
    }

    /* Add QUIC level log */
    if (ret == 0) {
        ret = picoquic_set_textlog(config->nodes[1]->quic, "basic_textlog.txt");
    }

    if (ret == 0){
        /* Add a test source to the configuration, and to the server */
        ret = test_media_publish(config->nodes[0], (uint8_t*)QUICRQ_TEST_BASIC_SOURCE, strlen(QUICRQ_TEST_BASIC_SOURCE), media_source_path, NULL, is_real_time, &config->sources[0].next_source_time);
        config->sources[0].srce_ctx = config->nodes[0]->first_source;
    }

    if (ret == 0) {
        /* Create a quirq connection context on client */
        cnx_ctx = quicrq_test_basic_create_cnx(config, 1, 0);
        if (cnx_ctx == NULL) {
            ret = -1;
        }
    }

    if (ret == 0) {
        /* Create a subscription to the test source on client */
        ret = test_media_subscribe(cnx_ctx, (uint8_t*)QUICRQ_TEST_BASIC_SOURCE, strlen(QUICRQ_TEST_BASIC_SOURCE), use_datagrams, QUICRQ_TEST_BASIC_RESULT, QUICRQ_TEST_BASIC_LOG);
    }

    while (ret == 0 && nb_inactive < max_inactive && config->simulated_time < max_time) {
        /* Run the simulation. Monitor the connection. Monitor the media. */
        int is_active = 0;

        ret = quicrq_test_loop_step(config, &is_active);

        nb_steps++;
        if (is_active) {
            nb_inactive = 0;
        }
        else {
            nb_inactive++;
        }
        /* TODO: if the media is received, exit the loop */
        if (config->nodes[1]->first_cnx == NULL) {
            break;
        } else if (config->nodes[1]->first_cnx->first_stream == NULL || config->nodes[1]->first_cnx->first_stream->is_server_finished) {
            if (!is_closed) {
                /* Client is done. Close connection without waiting for timer */
                ret = picoquic_close(config->nodes[1]->first_cnx->cnx, 0);
                is_closed = 1;
            }
        }
    }

    /* Clear everything. */
    if (config != NULL) {
        quicrq_test_config_delete(config);
    }
    /* Verify that media file was received correctly */
    if (ret == 0) {
        ret = quicrq_compare_media_file(QUICRQ_TEST_BASIC_RESULT, media_source_path);
    }

    return ret;
}

/* Basic connection test, using streams, not real time. */
int quicrq_basic_test()
{
    return quicrq_basic_test_one(0, 0);
}

/* Basic connection test, using streams, real time. */
int quicrq_basic_rt_test()
{
    return quicrq_basic_test_one(1, 0);
}

/* Basic datagram test. Same as the basic test, but using datagrams instead of streams. */
int quicrq_basic_datagram_test()
{
    return quicrq_basic_test_one(1, 1);
}