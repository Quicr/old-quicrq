/* quicr demo app */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#ifdef _WINDOWS
#define WIN32_LEAN_AND_MEAN
#include "getopt.h"
#include <WinSock2.h>
#include <Windows.h>

#define SERVER_CERT_FILE "certs\\cert.pem"
#define SERVER_KEY_FILE  "certs\\key.pem"

#else /* Linux */

#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>

#ifndef __USE_XOPEN2K
#define __USE_XOPEN2K
#endif
#ifndef __USE_POSIX
#define __USE_POSIX
#endif
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#endif

#include <picoquic.h>
#include <picosocks.h>
#include <picoquic_config.h>
#include <picoquic_packet_loop.h>
#include <autoqlog.h>
#include <performance_log.h>
#include "quicrq.h"
#include "quicrq_relay.h"
#include "quicrq_test_internal.h"

typedef enum {
    quicrq_app_mode_none = 0,
    quicrq_app_mode_server,
    quicrq_app_mode_relay,
    quicrq_app_mode_client
} quicrq_app_mode_enum;

typedef struct st_quicrq_app_loop_cb_t {
    quicrq_app_mode_enum mode;
    quicrq_ctx_t* qr_ctx;
    size_t nb_test_sources;
    size_t allocated_test_sources;
    test_media_object_source_context_t** test_source_ctx;
} quicrq_app_loop_cb_t;

int quicrq_app_check_source_time(quicrq_app_loop_cb_t* cb_ctx,
    packet_loop_time_check_arg_t* time_check_arg)
{
    int ret = 0;
    uint64_t next_time = time_check_arg->current_time + time_check_arg->delta_t;
    uint64_t cache_next_time;

    for (size_t i = 0; ret== 0 && i < cb_ctx->nb_test_sources; i++) {
        /* Find the time at which the next object will be ready. */
        uint64_t next_source_time = test_media_object_source_next_time(
            cb_ctx->test_source_ctx[i], time_check_arg->current_time);
        if (next_source_time < next_time) {
            next_time = next_source_time;
            if (next_time > time_check_arg->current_time) {
                /* Wait until the next event for the most urgent source */
                time_check_arg->delta_t = next_time - time_check_arg->current_time;
            }
            else {
                /* If time has arrived, push the next packet, or packets.
                 * Mark the wait time as zero, since there is certainly something to send.
                 */
                int is_active = 0;

                next_time = time_check_arg->current_time;
                time_check_arg->delta_t = 0;
                ret = test_media_object_source_iterate(cb_ctx->test_source_ctx[i],
                    time_check_arg->current_time, &is_active);
            }
        }
    }
    cache_next_time = quicrq_time_check(cb_ctx->qr_ctx, time_check_arg->current_time);
    if (cache_next_time < next_time) {
        if (cache_next_time > time_check_arg->current_time) {
            /* Wait until the next event for the most urgent source */
            time_check_arg->delta_t = cache_next_time - time_check_arg->current_time;
        }
        else {
            time_check_arg->delta_t = 0;
        }
    }

    return ret;
}

int quicrq_app_loop_cb_check_fin(quicrq_app_loop_cb_t* cb_ctx)
{
    int ret = 0;

    /* if a client, exit the loop if connection is gone. */
    quicrq_cnx_ctx_t* cnx_ctx = quicrq_first_connection(cb_ctx->qr_ctx);
    if (cnx_ctx == NULL || quicrq_is_cnx_disconnected(cnx_ctx)) {
        ret = PICOQUIC_NO_ERROR_TERMINATE_PACKET_LOOP;
    }
    else if (!quicrq_cnx_has_stream(cnx_ctx)) {
        ret = quicrq_close_cnx(cnx_ctx);
    }
    return ret;
}

int quicrq_app_loop_cb(picoquic_quic_t* quic, picoquic_packet_loop_cb_enum cb_mode,
    void* callback_ctx, void* callback_arg)
{
    int ret = 0;
    quicrq_app_loop_cb_t* cb_ctx = (quicrq_app_loop_cb_t*)callback_ctx;

    if (cb_ctx == NULL) {
        ret = PICOQUIC_ERROR_UNEXPECTED_ERROR;
        if (quic == NULL) {
            DBG_PRINTF("%s", "Quic context not set.");
        }
    }
    else {
        switch (cb_mode) {
        case picoquic_packet_loop_ready:
            fprintf(stdout, "Waiting for packets.\n");
            if (callback_arg != NULL) {
                picoquic_packet_loop_options_t* options = (picoquic_packet_loop_options_t*)callback_arg;
                options->do_time_check = 1;
            }
            break;
        case picoquic_packet_loop_after_receive:
            /* Post receive callback */
            /* if a client, exit the loop if connection is gone. */
            if (cb_ctx->mode == quicrq_app_mode_client) {
                ret = quicrq_app_loop_cb_check_fin(cb_ctx);
            }
            break;
        case picoquic_packet_loop_after_send:
            /* if a client, exit the loop if connection is gone. */
            if (cb_ctx->mode == quicrq_app_mode_client) {
                /* if a client, exit the loop if connection is gone. */
                ret = quicrq_app_loop_cb_check_fin(cb_ctx);
            }
            break;
        case picoquic_packet_loop_port_update:
            break;
        case picoquic_packet_loop_time_check:
            /* check local test sources, push data if ready  */
            ret = quicrq_app_check_source_time(cb_ctx, (packet_loop_time_check_arg_t*)callback_arg);
            break;
        default:
            ret = PICOQUIC_ERROR_UNEXPECTED_ERROR;
            break;
        }
    }
    return ret;
}

/* Parsing of scenarios.
 * Scenarios are only expected on the client and the server.
 * On both, scenarios of type "post" cause the creation of a local source.
 * On clients, scenarios of type "get" cause a subscription request, and
 * scenarios of type "post" cause a post request.
 * 
 * The syntax is expressed as: 
 * *{{'get'|'post'}':'<url>':'<path>[':'<log_path>]';'}
 */
char const* quic_app_scenario_parse_method(char const* scenario, int* method)
{
    char const* next_char = scenario;
    *method = -1;
    if (strncmp(scenario, "get:", 4) == 0) {
        next_char += 4;
        *method = 0;
    }
    else if (strncmp(scenario, "post:", 5) == 0) {
        next_char += 5;
        *method = 1;
    }
    else
    {
        next_char = NULL;
    }
    return next_char;
}

char const* quic_app_scenario_parse_field(char const* next_char, char const ** str, size_t * length, int maybe_last)
{
    size_t len = 0;
    char const* first_char = next_char;
    *str = next_char;
    while (next_char != NULL) {
        if (*next_char == ':') {
            next_char++;
            break;
        }
        if (*next_char == 0 || *next_char == ';') {
            if (!maybe_last) {
                fprintf(stderr, "String ends before \':\' : %s\n", first_char);
                next_char = NULL;
            }
            break;
        }
        else {
            len++;
            next_char++;
        }
    }
    *length = len;
    return next_char;
}

char const* quic_app_scenario_parse_string(char const* next_char, char * str, size_t max_length, int maybe_last)
{
    size_t len = 0;
    char const * first_char = next_char;

    while (next_char != NULL) {
        if (*next_char == ':') {
            next_char++;
            break;
        }
        if (*next_char == 0 || *next_char == ';') {
            if (!maybe_last) {
                next_char = NULL;
            }
            break;
        }
        else {
            if (len + 1 < max_length) {
                str[len] = *next_char;
            }
            else {
                fprintf(stderr, "String is long than %zu: %s\n", max_length, first_char);
            }
            len++;
            next_char++;
        }
    }
    str[len] = 0;
    return next_char;
}

int quicrq_app_add_source(quicrq_app_loop_cb_t* cb_ctx, uint8_t* url, size_t url_length,
    char const* media_source_path, uint64_t current_time)
{
    int ret = 0;
    if (cb_ctx->nb_test_sources >= cb_ctx->allocated_test_sources) {
        size_t new_nb = (cb_ctx->allocated_test_sources == 0) ? 8 : 2 * cb_ctx->allocated_test_sources;
        test_media_object_source_context_t** new_test_source_ctx =
            (test_media_object_source_context_t**)malloc(new_nb * sizeof(test_media_object_source_context_t*));
        if (new_test_source_ctx == NULL) {
            fprintf(stderr, "Out of memory\n");
            ret = -1;
        }
        else {
            memset(new_test_source_ctx, 0, new_nb * sizeof(test_media_object_source_context_t*));
            if (cb_ctx->test_source_ctx != NULL) {
                if (cb_ctx->nb_test_sources > 0) {
                    memcpy(new_test_source_ctx, cb_ctx->test_source_ctx, cb_ctx->nb_test_sources * sizeof(test_media_object_source_context_t*));
                }
                free(cb_ctx->test_source_ctx);
            }
            cb_ctx->test_source_ctx = new_test_source_ctx;
            cb_ctx->allocated_test_sources = new_nb;
        }
    }

    if (ret == 0) {
        cb_ctx->test_source_ctx[cb_ctx->nb_test_sources] = test_media_object_source_publish(cb_ctx->qr_ctx, (uint8_t*)url, url_length,
            media_source_path, NULL, 1, current_time);
        if (cb_ctx->test_source_ctx[cb_ctx->nb_test_sources] == NULL) {
            fprintf(stderr, "Cannot allocate source number %zu\n", cb_ctx->nb_test_sources + 1);
            ret = -1;
        }
        else {
            cb_ctx->nb_test_sources++;
        }
    }

    return ret;
}

void quicrq_app_free_sources(quicrq_app_loop_cb_t* cb_ctx)
{
    if (cb_ctx->test_source_ctx != NULL) {
        free(cb_ctx->test_source_ctx);
        cb_ctx->test_source_ctx = NULL;
    }
}

char const* quic_app_scenario_parse_line(quicrq_app_loop_cb_t* cb_ctx, char const* scenario,
    uint64_t current_time, int use_datagrams, quicrq_cnx_ctx_t * cnx_ctx)
{
    /* parse the current scenario until end of line or semicolon */
    char const* next_char = scenario;
    int method;
    size_t url_length = 0;
    char const* url = NULL;
    char path[512];
    char log_path[512];

    path[0] = 0;
    log_path[0] = 0;

    if ((next_char = quic_app_scenario_parse_method(next_char, &method)) == NULL ||
        (next_char = quic_app_scenario_parse_field(next_char, &url, &url_length, 0)) == NULL ||
        (next_char = quic_app_scenario_parse_string(next_char, path, sizeof(path), 1)) == NULL ||
        (next_char = quic_app_scenario_parse_string(next_char, log_path, sizeof(log_path), 1)) == NULL) {
        /* Syntax error */
        fprintf(stderr, "Incorrect scenario: %s\n", scenario);
    }
    else {
        int ret = 0;
        if (method == 1) {
            /* This is a post. Create a media source */
            if (quicrq_app_add_source(cb_ctx, (uint8_t*)url, url_length,
                path, current_time) != 0) {
                next_char = NULL;
            }
            else if (cb_ctx->mode == quicrq_app_mode_client) {
                /* Post the media */
                ret = quicrq_cnx_post_media(cnx_ctx, (uint8_t*)url, url_length, use_datagrams);
                if (ret != 0) {
                    fprintf(stderr, "Cannot post url for scenario: %s\n", scenario);
                }
            }
        }
        else {
            /* This is a get */
            if (cb_ctx->mode == quicrq_app_mode_client) {
                /* Subscribe to the media */
                if (log_path[0] == 0) {
                    picoquic_sprintf(log_path, sizeof(log_path), NULL, "%s.csv", path);
                }
                if (ret == 0) {
                    test_object_stream_ctx_t* object_stream_ctx = NULL;
                    object_stream_ctx = test_object_stream_subscribe(cnx_ctx, (const uint8_t*)url, url_length, use_datagrams, path, log_path);
                    if (object_stream_ctx == NULL) {
                        ret = -1;
                    }
                }
                if (ret != 0) {
                    fprintf(stderr, "Cannot subscribe to test media %s, ret = %d", path, ret);
                }
            }
            else {
                fprintf(stderr, "Must be client to get media: %s\n", scenario);
                ret = -1;
            }
        }
        if (ret == 0) {
            if (*next_char == ';') {
                next_char++;
            }
        }
        else {
            next_char = NULL;
        }
    }

    return next_char;
}

int quic_app_scenario_parse(quicrq_app_loop_cb_t* cb_ctx, char const* scenario,
    uint64_t current_time, int use_datagrams, quicrq_cnx_ctx_t* cnx_ctx)
{
    char const* next_char = scenario;

    while (next_char != NULL && *next_char != 0) {
        next_char = quic_app_scenario_parse_line(cb_ctx, next_char, current_time,
            use_datagrams, cnx_ctx);
    }

    return (next_char == NULL) ? -1 : 0;
}

int quic_app_loop(picoquic_quic_config_t* config,
    int mode,
    const char* server_name,
    int use_datagram,
    int server_port,
    char const* scenario)
{
    int ret = 0;

    /* Initialize the loop callback context */
    quicrq_app_loop_cb_t cb_ctx = { 0 };
    struct sockaddr_storage addr = { 0 };
    int is_name = 0;
    char const* sni = NULL;
    picoquic_quic_t* quic = NULL;
    quicrq_cnx_ctx_t* cnx_ctx = NULL;
    uint64_t current_time = picoquic_current_time();

    cb_ctx.qr_ctx = quicrq_create_empty();

    if (cb_ctx.qr_ctx == NULL) {
        ret = -1;
    }
    else {
        cb_ctx.mode = mode;

        if (config->alpn == NULL) {
            picoquic_config_set_option(config, picoquic_option_ALPN, QUICRQ_ALPN);
        }

        /* TODO: Verify that the ALPN configured corresponds to our application. */
        /* Create a picoquic context, using the configuration */
        quic = picoquic_create_and_configure(config,
            quicrq_callback, cb_ctx.qr_ctx,
            current_time, NULL);
        if (quic == NULL) {
            ret = -1;
        }
        else {
            /* Setting logs, etc. */
            quicrq_set_quic(cb_ctx.qr_ctx, quic);

            picoquic_set_key_log_file_from_env(quic);

            picoquic_set_mtu_max(quic, config->mtu_max);

            if (config->qlog_dir != NULL)
            {
                picoquic_set_qlog(quic, config->qlog_dir);
            }
            if (config->performance_log != NULL)
            {
                ret = picoquic_perflog_setup(quic, config->performance_log);
            }
        }
    }
    /* Set up a default receiver on the server */
    if (ret == 0 && mode == quicrq_app_mode_server) {
        quicrq_enable_origin(cb_ctx.qr_ctx, use_datagram);
    }

    /* If client or relay, resolve the address */
    if (ret == 0 && (mode == quicrq_app_mode_client || mode == quicrq_app_mode_relay)) {
        ret = picoquic_get_server_address(server_name, server_port, &addr, &is_name);
        if (ret != 0) {
            fprintf(stderr, "Cannot find address of %s\n", server_name);
        }
        else if (is_name != 0) {
            sni = server_name;
        }
    }
    /* If relay, enable relaying */
    if (ret == 0 && mode == quicrq_app_mode_relay) {
        ret = quicrq_enable_relay(cb_ctx.qr_ctx, sni, (struct sockaddr*)&addr, use_datagram);
        if (ret != 0) {
            fprintf(stderr, "Cannot initialize relay to %s\n", server_name);
        }
        else {
            fprintf(stdout, "Relaying to %s:%d\n", server_name, server_port);
        }
    }

    /* if client, create a connection to the upstream node so we can start the scenarios */
    if (ret == 0 && mode == quicrq_app_mode_client) {
        if ((cnx_ctx = quicrq_create_client_cnx(cb_ctx.qr_ctx, sni, (struct sockaddr *) &addr)) == NULL) {
            fprintf(stderr, "Cannot create connection to %s\n", server_name);
            ret = -1;
        }
    }

    /* if client or server, initialize all the local sources */
    if (ret == 0 && (mode == quicrq_app_mode_client || mode == quicrq_app_mode_server)) {
        if (scenario == NULL) {
            if (mode == quicrq_app_mode_client) {
                fprintf(stderr, "No scenario provided!\n");
                ret = -1;
            }
        }
        else {
            ret = quic_app_scenario_parse(&cb_ctx, scenario, current_time,
                use_datagram, cnx_ctx);
        }
    }

    /* If relay or origin, delete cached entries longer than 2 minute */
    quicrq_set_cache_duration(cb_ctx.qr_ctx, 120000000);

    /* Start the loop */
    if (ret == 0) {
#if _WINDOWS
        ret = picoquic_packet_loop_win(quic, config->server_port, 0, config->dest_if,
            config->socket_buffer_size, quicrq_app_loop_cb, &cb_ctx);
#else
        ret = picoquic_packet_loop(quic, config->server_port, 0, config->dest_if,
            config->socket_buffer_size, config->do_not_use_gso, quicrq_app_loop_cb, &cb_ctx);
#endif
    }

    /* And exit */
    printf("Quicrq_app loop exit, ret = %d (0x%x)\n", ret, ret);
    /* Release the media sources*/
    quicrq_app_free_sources(&cb_ctx);
    /* Free the quicrq context */
    quicrq_delete(cb_ctx.qr_ctx);

    return ret;
}

void usage()
{
    fprintf(stderr, "QUICRQ client, relay and server\n");
    fprintf(stderr, "Usage: quicrq_app <options> [mode] [server_name ['d'|'s'] port [scenario]] \n");
    fprintf(stderr, "  mode can be one of client, relay or server.\n");
    fprintf(stderr, "  For the client and relay mode, specify server_name and port,\n");
    fprintf(stderr, "  and either 'd' or 's' for datagram or stream mode.\n");
    fprintf(stderr, "  For the server and relay mode, use -p to specify the port,\n");
    fprintf(stderr, "  and also -c and -k for certificate and matching private key.\n");
    picoquic_config_usage();
    fprintf(stderr, "\nOn the client, the scenario argument specifies the media files\n");
    fprintf(stderr, "that should be retrieved (get) or published (post):\n");
    fprintf(stderr, "  *{{'get'|'post'}':'<url>':'<path>[':'<log_path>]';'}\n");
    fprintf(stderr, "where:\n");
    fprintf(stderr, "  <url>:      The name by which the media is known\n");
    fprintf(stderr, "  <path>:     The local file where to store (get) or read (post) the media.)\n");
    fprintf(stderr, "  <log_path>: The local file where to write statistics (get only).)\n");
    exit(1);
}

int main(int argc, char** argv)
{
    picoquic_quic_config_t config;
    char option_string[512];
    int opt;
    int ret = 0;
    quicrq_app_mode_enum mode = 0;
    const char* server_name = NULL;
    int use_datagram = 0;
    int server_port = -1;
    char const* scenario = NULL;
#ifdef _WINDOWS
    WSADATA wsaData = { 0 };
    (void)WSA_START(MAKEWORD(2, 2), &wsaData);
#endif
    fprintf(stdout, "QUICRQ Version %s, Picoquic Version %s\n", QUICRQ_VERSION, PICOQUIC_VERSION);

    picoquic_config_init(&config);
    ret = picoquic_config_option_letters(option_string, sizeof(option_string), NULL);

    if (ret == 0) {
        /* Get the parameters */
        while ((opt = getopt(argc, argv, option_string)) != -1) {
            switch (opt) {
            default:
                if (picoquic_config_command_line(opt, &optind, argc, (char const**)argv, optarg, &config) != 0) {
                    usage();
                }
                break;
            }
        }
    }

    /* Simplified style params */
    if (optind < argc) {
        char const* a_mode = argv[optind++];
        if (strcmp(a_mode, "client") == 0) {
            mode = quicrq_app_mode_client;
        }
        else if (strcmp(a_mode, "relay") == 0) {
            mode = quicrq_app_mode_relay;
        }
        else if (strcmp(a_mode, "server") == 0) {
            mode = quicrq_app_mode_server;
        }
    }

    if (mode == quicrq_app_mode_none){
        usage();
    }
    else
    {
        if (mode != quicrq_app_mode_server) {
            if (optind + 3 > argc) {
                usage();
            }
            else {
                char const* a_d_s;
                server_name = argv[optind++];
                a_d_s = argv[optind++];
                server_port = atoi(argv[optind++]);

                if (strcmp(a_d_s, "d") == 0) {
                    use_datagram = 1;
                }
                else if (strcmp(a_d_s, "s") == 0) {
                    use_datagram = 0;
                }
                else {
                    usage();
                }
                if (server_port <= 0) {
                    fprintf(stderr, "Invalid server port: %s\n", optarg);
                    usage();
                }
            }
        }

        if (optind < argc) {
            if (mode != quicrq_app_mode_relay) {
                scenario = argv[optind++];
            }
            else {
                fprintf(stderr, "No scenario expected in relay mode: %s\n", optarg);
                usage();
            }
        }
        else if (mode == quicrq_app_mode_client) {
            fprintf(stderr, "Scenario expected in client mode!\n");
            usage();
        }
        
        if (optind < argc) {
            fprintf(stderr, "Extra argument not expected: %s\n", optarg);
            usage();
        }
    }

    /* Run */
    ret = quic_app_loop(&config, mode, server_name, use_datagram, server_port, scenario);
    /* Clean up */
    picoquic_config_clear(&config);
    /* Exit */
    exit(ret);
}
