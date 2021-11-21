/* Handling of a relay
 */

#include "quicrq.h"
#include "quicrq_internal.h"

/* A relay is a specialized node, acting both as client when acquiring a media
 * segment and as server when producing data.
 * 
 * The client creates a list of media frames. For simplification, the server will
 * only deal with the media frames that are fully received. When a media frame is
 * fully received, it becomes available. We may consider a difference in
 * availability between "in-order" and "out-of-sequence" availablity, which
 * may need to be reflected in the contract between connection and sources.
 */

int quicrq_relay_consumer_cb(
    quicrq_media_consumer_enum action,
    void* media_ctx,
    uint64_t current_time,
    const uint8_t* data,
    uint64_t frame_id,
    uint64_t offset,
    int is_last_segment,
    size_t data_length)
{
    int ret = 0;
#if 0
    test_media_consumer_context_t* cons_ctx = (test_media_consumer_context_t*)media_ctx;

    switch (action) {
    case quicrq_media_datagram_ready:
        ret = test_media_datagram_input(media_ctx, current_time, data, frame_id, (size_t)offset, is_last_segment, data_length);

        if (ret == 0 && cons_ctx->is_finished) {
            ret = quicrq_consumer_finished;
        }
        break;
    case quicrq_media_final_frame_id:
        test_media_consumer_learn_final_frame_id(media_ctx, frame_id);

        if (ret == 0 && cons_ctx->is_finished) {
            ret = quicrq_consumer_finished;
        }
        break;
    case quicrq_media_close:
        ret = test_media_consumer_close(media_ctx);
        break;
    default:
        ret = -1;
        break;
    }
#endif
    return ret;
}
