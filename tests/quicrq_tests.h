/* Declaration of the test functions for the QUICR TEST library
 */
#ifndef QUICRQ_TEST_H
#define QUICRQ_TEST_H

#ifdef __cplusplus
extern "C" {
#endif
    extern char const* quicrq_test_solution_dir;

    int quicrq_basic_test();
    int proto_msg_test();
    int quicrq_media_video1_test();
    int quicrq_media_video1_rt_test();
    int quicrq_media_audio1_test();
    int quicrq_basic_rt_test();
    int quicrq_congestion_basic_test();
    int quicrq_congestion_basic_recv_test();
    int quicrq_congestion_basic_loss_test();
    int quicrq_congestion_basic_zero_test();
    int quicrq_congestion_basic_half_test();
    int quicrq_congestion_datagram_test();
    int quicrq_congestion_datagram_loss_test();
    int quicrq_congestion_datagram_recv_test();
    int quicrq_congestion_datagram_rloss_test();
    int quicrq_congestion_datagram_zero_test();
    int quicrq_congestion_datagram_half_test();
    int quicrq_datagram_basic_test();
    int quicrq_datagram_loss_test();
    int quicrq_datagram_extra_test();
    int quicrq_basic_client_test();
    int quicrq_datagram_client_test();
    int quicrq_datagram_limit_test();
    int quicrq_datagram_unsubscribe_test();
    int quicrq_twomedia_test();
    int quicrq_twomedia_datagram_test();
    int quicrq_twomedia_datagram_loss_test();
    int quicrq_twomedia_client_test();
    int quicrq_twomedia_datagram_client_test();
    int quicrq_twomedia_datagram_client_loss_test();
    int quicrq_media_object_noloss();
    int quicrq_media_object_loss();
    int quicrq_relay_basic_test();
    int quicrq_relay_datagram_test();
    int quicrq_relay_datagram_loss_test();
    int quicrq_relay_basic_client_test();
    int quicrq_relay_datagram_client_test();
    int quicrq_subscribe_basic_test();
    int quicrq_subscribe_relay1_test();
    int quicrq_subscribe_relay2_test();
    int quicrq_subscribe_relay3_test();
    int quicrq_subscribe_datagram_test();
    int quicrq_subscribe_client_test();
    int quicrq_triangle_basic_test();
    int quicrq_triangle_basic_loss_test();
    int quicrq_triangle_datagram_test();
    int quicrq_triangle_datagram_loss_test();
    int quicrq_triangle_datagram_extra_test();
    int quicrq_triangle_start_point_test();
    int quicrq_triangle_start_point_s_test();
    int quicrq_triangle_cache_test();
    int quicrq_triangle_cache_loss_test();
    int quicrq_triangle_cache_stream_test();
    int quicrq_triangle_intent_test();
    int quicrq_triangle_intent_nc_test();
    int quicrq_triangle_intent_datagram_test();
    int quicrq_triangle_intent_dg_nc_test();
    int quicrq_triangle_intent_loss_test();
    int quicrq_triangle_intent_next_test();
    int quicrq_triangle_intent_next_s_test();
    int quicrq_triangle_intent_that_test();
    int quicrq_triangle_intent_that_s_test();
    int quicrq_pyramid_basic_test();
    int quicrq_pyramid_datagram_test();
    int quicrq_pyramid_datagram_loss_test();
    int quicrq_pyramid_datagram_client_test();
    int quicrq_pyramid_datagram_delay_test();
    int quicrq_pyramid_publish_delay_test();
    int quicrq_twoways_basic_test();
    int quicrq_twoways_datagram_test();
    int quicrq_twoways_datagram_loss_test();
    int quicrq_twomedia_tri_stream_test();
    int quicrq_twomedia_tri_datagram_test();
    int quicrq_twomedia_tri_later_test();
    int quicrq_threelegs_basic_test();
    int quicrq_threelegs_datagram_test();
    int quicrq_threelegs_datagram_loss_test();
    int quicrq_fourlegs_basic_test();
    int quicrq_fourlegs_basic_last_test();
    int quicrq_fourlegs_datagram_test();
    int quicrq_fourlegs_datagram_last_test();
    int quicrq_fourlegs_datagram_loss_test();
    int quicrq_fragment_cache_fill_test();
    int quicrq_get_addr_test();
    int quicrq_warp_basic_test();
    int quicrq_warp_basic_client_test();
    int quicrq_relay_basic_warp_test();

#ifdef __cplusplus
}
#endif
#endif /* QUICRQ_TEST_H */
