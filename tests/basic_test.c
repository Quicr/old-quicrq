#include "quicrq.h"
#include "quicrq_internal.h"
#include "quicrq_tests.h"
#include "quicrq_test_internal.h"
#include "picoquic_utils.h"

#ifdef _WINDOWS
#ifdef _WINDOWS64
#define QUICRQ_PICOQUIC_DEFAULT_SOLUTION_DIR "..\\..\\..\\..\\picoquic\\"
#define QUICRQ_DEFAULT_SOLUTION_DIR "..\\..\\..\\"
#else
#define QUICRQ_PICOQUIC_DEFAULT_SOLUTION_DIR "..\\..\\..\\picoquic\\"
#define QUICRQ_DEFAULT_SOLUTION_DIR "..\\..\\"
#endif
#else
#define QUICRQ_PICOQUIC_DEFAULT_SOLUTION_DIR "../picoquic/"
#define QUICRQ_DEFAULT_SOLUTION_DIR "./"
#endif

char const* quicrq_test_picoquic_solution_dir = QUICRQ_PICOQUIC_DEFAULT_SOLUTION_DIR;
char const* quicrq_test_solution_dir = QUICRQ_DEFAULT_SOLUTION_DIR;


/* Test configuration: nodes, sources, addresses and links.
 * Each source is connected to a node.
 * Each node is connected to departure links, with a specified address.
 * Each link has a list of reachable destinations, specifying node context and address.
 */

typedef struct st_quicrq_test_attach_t {
    int node_id;
    int link_to_id;
    int link_from_id;
    struct sockaddr_storage node_addr;
} quicrq_test_attach_t;

typedef struct st_quicrq_test_source_t {
    /* TODO: relation between source and node */
    uint64_t next_source_time;
} quicrq_test_source_t;

typedef struct st_quicrq_test_config_t {
    uint64_t simulated_time;
    char test_server_cert_file[512];
    char test_server_key_file[512];
    char test_server_cert_store_file[512];
    char ticket_encryption_key[16];
    int nb_nodes;
    quicrq_ctx_t** nodes;
    int nb_links;
    picoquictest_sim_link_t** links;
    int nb_attachments;
    quicrq_test_attach_t* attachments;
    int nb_sources;
    quicrq_test_source_t* sources;
} quicrq_test_config_t;

/* Find arrival context by link ID and destination address */
int quicrq_test_find_recv_link(quicrq_test_config_t* config, int link_id, struct sockaddr* addr)
{
    int attach_id = -1;

    for (int j_attach = 0; j_attach < config->nb_attachments; j_attach++) {
        if (config->attachments[j_attach].link_to_id == link_id &&
            picoquic_compare_addr((struct sockaddr*)&config->attachments[j_attach].node_addr, addr) == 0) {
            attach_id = j_attach;
            break;
        }
    }
    return (attach_id);
}

/* Find departure context by nodeid and destination address.
 */
int quicrq_test_find_send_link(quicrq_test_config_t * config, int node_id, struct sockaddr* addr)
{
    int attach_id = -1;
    for (int i_attach = 0; i_attach < config->nb_attachments; i_attach++) {
        if (config->attachments[i_attach].node_id == node_id) {
            int link_id = config->attachments[i_attach].link_from_id;
            if (quicrq_test_find_recv_link(config, config->attachments[i_attach].link_from_id, addr) >= 0) {
                attach_id = i_attach;
                break;
            }
        }
    }
    return attach_id;
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
            int attach_id = quicrq_test_find_send_link(config, node_id, (struct sockaddr*)&packet->addr_to);

            if (attach_id >= 0) {
                if (packet->addr_from.ss_family == AF_UNSPEC) {
                    picoquic_store_addr(&packet->addr_from, (struct sockaddr*)&config->attachments[attach_id].node_addr);
                }
                *is_active = 1;
                picoquictest_sim_link_submit(config->links[config->attachments[attach_id].link_to_id], packet, config->simulated_time);
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
        int attach_id = quicrq_test_find_recv_link(config, link_id, (struct sockaddr*)&packet->addr_to);

        if (attach_id >= 0) {
            *is_active = 1;
            
            ret = picoquic_incoming_packet(config->nodes[config->attachments[attach_id].node_id]->quic,
                packet->bytes, (uint32_t)packet->length,
                (struct sockaddr*)&packet->addr_from,
                (struct sockaddr*)&packet->addr_to, 0, 0,
                config->simulated_time);
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
        uint64_t quic_time = picoquic_get_next_wake_time(config->nodes[i]->quic, config->simulated_time);
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
            /* TODO: source processing */
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

        memset(config, 0, sizeof(config));
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
        config->nodes[0] = quicrq_create(NULL,
            config->test_server_cert_file, config->test_server_key_file, NULL, NULL, NULL,
            config->ticket_encryption_key, sizeof(config->ticket_encryption_key),
            &config->simulated_time);
        config->nodes[1] = quicrq_create(NULL,
            NULL, NULL, config->test_server_cert_store_file, NULL, NULL,
            NULL, 0, &config->simulated_time);
        if (config->nodes[0] == NULL || config->nodes[1] == NULL) {
            quicrq_test_config_delete(config);
            config = NULL;
        }
    }
    if (config != NULL) {
        /* Populate the attachments */
        config->attachments[0].link_from_id = 1;
        config->attachments[0].link_to_id = 0;
        config->attachments[0].node_id = 0;
        /* TODO: initiate source */
        config->attachments[0].link_from_id = 0;
        config->attachments[0].link_to_id = 1;
        config->attachments[0].node_id = 1;
    }
    return config;
}

/* Basic connection test */
int quicrq_basic_test()
{
    int ret = 0;
    quicrq_test_config_t* config = quicrq_test_basic_config_create();

    if (config == NULL) {
        ret = -1;
    }
    else {
        quicrq_test_config_delete(config);
    }

    return ret;
}
