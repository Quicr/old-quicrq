/* Declaration of the test functions for the QUICR TEST library
 */
#ifndef QUICRQ_TEST_H
#define QUICRQ_TEST_H

#ifdef __cplusplus
extern "C" {
#endif
    extern char const* quicrq_test_picoquic_solution_dir;
    extern char const* quicrq_test_solution_dir;

    int quicrq_basic_test();
    int proto_msg_test();
    int quicrq_media_video1_test();
    int quicrq_media_video1_rt_test();
    int quicrq_media_audio1_test();
    int quicrq_media_source_test();
    int quicrq_media_source_rt_test();
    int quicrq_media_object_publish_test();
    int quicrq_media_object_source_test();
    int quicrq_media_object_source_rt_test();
    int quicrq_object_stream_test();
    int quicrq_object_stream_rt_test();
    int quicrq_basic_rt_test();
    int quicrq_datagram_basic_test();
    int quicrq_datagram_loss_test();
    int quicrq_datagram_extra_test();
    int quicrq_basic_client_test();
    int quicrq_datagram_client_test();
    int quicrq_datagram_limit_test();
    int quicrq_twomedia_test();
    int quicrq_twomedia_datagram_test();
    int quicrq_twomedia_datagram_loss_test();
    int quicrq_media_object_noloss();
    int quicrq_media_object_loss();
    int quicrq_relay_basic_test();
    int quicrq_relay_datagram_test();
    int quicrq_relay_datagram_loss_test();
    int quicrq_relay_basic_client_test();
    int quicrq_relay_datagram_client_test();
    int quicrq_triangle_basic_test();
    int quicrq_triangle_basic_loss_test();
    int quicrq_triangle_datagram_test();
    int quicrq_triangle_datagram_loss_test();
    int quicrq_triangle_datagram_extra_test();
    int quicrq_pyramid_basic_test();
    int quicrq_pyramid_datagram_test();
    int quicrq_pyramid_datagram_loss_test();
    int quicrq_pyramid_datagram_client_test();
    int quicrq_pyramid_datagram_delay_test();
    int quicrq_pyramid_publish_delay_test();
    int quicrq_twoways_basic_test();
    int quicrq_twoways_datagram_test();
    int quicrq_twoways_datagram_loss_test();
    int quicrq_threelegs_basic_test();
    int quicrq_threelegs_datagram_test();
    int quicrq_threelegs_datagram_loss_test();
    int quicrq_fourlegs_basic_test();
    int quicrq_fourlegs_basic_last_test();
    int quicrq_fourlegs_datagram_test();
    int quicrq_fourlegs_datagram_last_test();
    int quicrq_fourlegs_datagram_loss_test();
    int quicrq_relay_cache_fill_test();
    int quicrq_get_addr_test();

#ifdef __cplusplus
}
#endif
#endif /* QUICRQ_TEST_H */