/* Handling of the congestion control algorithms */
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include "picoquic_utils.h"
#include "picosplay.h"
#include "quicrq.h"
#include "quicrq_reassembly.h"
#include "quicrq_internal.h"
#include "quicrq_fragment.h"


/* Handle delay based congestion.
* This should be done per connection, at least once per RTT.
* Check whether there is congestion, and also check the highest (least urgent)
* priority level among the streams.
* 
* There are two marks and a priority level
* - has_backlog:
*    set if one stream reports backlog.
*    cleared at beginning of congestion epoch.
* - is_congested:
*    set initially when the first backlog is reported.
*    cleared when not congested anymore
* - priority_threshold
*    packets at this or higher threshold are skipped.
* 
* The state also includes the "start of epoch" time, and the "old priority
* threshold
*    
* 
* The marks are evaluated:
* - when the first congestion is reported (has_backlog && !is_congested)
*     - this starts an epoch.
* - at the beginning of every new epoch.
*     - if this is the first epoch for this priority, do nothing
*       because the priority had no observable effect.
*     - if backlog reported, and threshold > 128, decrease threshold
*     - else if no backlog reported during the last epoch, increase the threshold
*         - if threshold larger than max flag, clear "is_congested".
* - in any case, reset "has_backlog", "old threshold", and epoch time.
*  
*/
int quicrq_congestion_check_per_cnx(quicrq_cnx_ctx_t* cnx_ctx, uint8_t flags, int has_backlog, uint64_t current_time)
{
    int should_skip = 0;

    /* Update the 'worst flag for the connection' */
    if (flags > cnx_ctx->congestion.max_flags && flags != 0xff) {
        cnx_ctx->congestion.max_flags = flags;
    }
    cnx_ctx->congestion.has_backlog |= has_backlog;

    if (!cnx_ctx->congestion.is_congested) {
        if (has_backlog) {
            /* Enter the congested state */
            cnx_ctx->congestion.is_congested = 1;
            cnx_ctx->congestion.has_backlog = 0;
            cnx_ctx->congestion.priority_threshold = cnx_ctx->congestion.max_flags;
            cnx_ctx->congestion.old_priority_threshold = 0xff;
        }
    } else if (current_time >= cnx_ctx->congestion.congestion_check_time) {
        /* Check the epoch */
        uint8_t old_priority_threshold = cnx_ctx->congestion.priority_threshold;

        if (cnx_ctx->congestion.old_priority_threshold != cnx_ctx->congestion.priority_threshold) {
            /* The threshold was changed at the last epoch check, so
            * congestion would not reflect the next threshold. Do nothing. */
        } else if (cnx_ctx->congestion.has_backlog) {
            /* if congested, set threshold priority to lower value */
            if (cnx_ctx->congestion.priority_threshold > 0x80) {
                cnx_ctx->congestion.priority_threshold -= 1;
            }
        }
        else {
            if (cnx_ctx->congestion.priority_threshold < cnx_ctx->congestion.max_flags) {
                cnx_ctx->congestion.priority_threshold += 1;
            }
            else {
                cnx_ctx->congestion.is_congested = 0;
            }
        }
        /* Reset the values to prepare the next epoch */
        cnx_ctx->congestion.old_priority_threshold = old_priority_threshold;
        cnx_ctx->congestion.has_backlog = 0;
        cnx_ctx->congestion.congestion_check_time += 50000; /* TODO: should be RTT of connection */
    }
    /* Evaluate whether this packet should be skipped */
    if (cnx_ctx->qr_ctx->congestion_control_mode != 0 && cnx_ctx->congestion.is_congested && flags >= cnx_ctx->congestion.priority_threshold) {
        should_skip = 1;
    }
    return should_skip;
}
/* Handle Group Based congestion:
 * 
 * When congestion is experienced, group based congestion drops the packets
 * belonging to all but the latest group. This will cause receivers to jump
 * ahead to the latest group. It avoids reliance on priority markings per
 * packets, and is only indirectly linked to scheduling priorities. Scheduling
 * determines which media stream is sent first, and thus which media stream
 * will experience queues. Congestion control only looks at these queues.
 * 
 * Congestion is detected when the current group ID is lower than the
 * latest group ID, and the transmission is more than 5 packets behind
 * the latest packet available. 
 * 
 * We are concerned about the special case of audio streams, which send
 * each packet in a group by itself. Congestion then is only detected if
 * the current group ID is 5 groups behind the latest one.
 * 
 * Once congestion is detected, the algorithm sets an "end of congestion"
 * mark to the next group ID. Packets in groups below that mark will be
 * automatically dropped.
 */

int quicrq_compute_group_mode_congestion(quicrq_fragment_publisher_context_t* media_ctx, uint64_t current_group_id, uint64_t current_object_id)
{
    int has_backlog = 0;
    int should_drop = 0;
    const uint64_t backlog_threshold = 5;

    if (current_group_id < media_ctx->end_of_congestion_group_id) {
        should_drop = 1;
    } else {
        quicrq_fragment_cache_t* cache_ctx = media_ctx->cache_ctx;

        if (current_group_id < cache_ctx->next_group_id) {
            /* Compute the size of the backlog */
            uint64_t backlog = cache_ctx->next_object_id;
            uint64_t previous_group_id = cache_ctx->next_group_id - 1;
            uint64_t previous_group_size = quicrq_fragment_get_object_count(cache_ctx, previous_group_id - 1);
            if (previous_group_size == 0) {
                /* Next group size not known yet. Do not detect congestion. */
                backlog = 0;
            } else {
                while (previous_group_id > current_group_id) {
                    previous_group_id--;
                    backlog += previous_group_size;
                    if (backlog >= backlog_threshold) {
                        break;
                    }
                    previous_group_size = quicrq_fragment_get_object_count(cache_ctx, previous_group_id - 1);
                    if (previous_group_size == 0) {
                        previous_group_size = 1;
                    }
                }
                if (previous_group_size > current_object_id) {
                    /* The only case in which previous_group_size is not the current group is when 
                     * backlog >= backlog_threshold. We can thus add the extra count here without
                     * worrying too much.
                     */
                    backlog += previous_group_size - current_object_id;
                }
                if (backlog >= backlog_threshold) {
                    should_drop = 1;
                    media_ctx->end_of_congestion_group_id = current_group_id + 1;
                }
            }
        }
    }
    return should_drop;
}

/* Evaluation of congestion for single stream transmission
 */
int quicrq_evaluate_stream_congestion(quicrq_fragment_publisher_context_t* media_ctx, uint64_t current_time)
{
    int is_object_skipped = 0;
    int has_backlog = 0;
    const uint64_t backlog_threshold = 5;

    switch (media_ctx->congestion_control_mode) {
    case quicrq_congestion_control_none:
        break;
    case quicrq_congestion_control_group:
        /* TODO: compute group mode congestion control */
        break;
    case quicrq_congestion_control_delay:
    default:
        if (media_ctx->current_offset > 0 || media_ctx->length_sent > 0) {
            has_backlog = media_ctx->has_backlog;
        }
        else if (media_ctx->current_group_id < media_ctx->cache_ctx->next_group_id ||
            (media_ctx->current_group_id == media_ctx->cache_ctx->next_group_id &&
                media_ctx->current_object_id + backlog_threshold < media_ctx->cache_ctx->next_object_id)) {
            has_backlog = 1;
            media_ctx->has_backlog = 1;
        }
        else {
            has_backlog = 0;
            media_ctx->has_backlog = 0;
        }
        /* Check the cache time, compare to current time, determine congestion */
        is_object_skipped = quicrq_congestion_check_per_cnx(media_ctx->stream_ctx->cnx_ctx,
            media_ctx->current_fragment->flags, has_backlog, current_time);
        break;
    }
    return is_object_skipped;
}
/* Evaluation of congestion in warp mode */
int quicrq_evaluate_warp_congestion(quicrq_uni_stream_ctx_t* uni_stream_ctx, quicrq_fragment_publisher_context_t* media_ctx, 
    size_t next_object_size, uint8_t flags, uint64_t current_time)
{
    int should_skip = 0;
    int has_backlog = 0;
    const uint64_t backlog_threshold = 5;
    quicrq_fragment_cache_t* cache_ctx = media_ctx->cache_ctx;

    if (flags == 0xff && next_object_size == 0) {
        /* This object was marked skipped at a previous relay */
        should_skip = 1;
    }
    else {
        switch (media_ctx->congestion_control_mode) {
        case quicrq_congestion_control_none:
            break;
        case quicrq_congestion_control_group:
            /* TODO: compute group mode congestion control */
            break;
        case quicrq_congestion_control_delay:
        default:
            /* Check whether there is ongoing congestion */
            if (uni_stream_ctx->current_group_id < cache_ctx->next_group_id ||
                (uni_stream_ctx->current_group_id == cache_ctx->next_group_id &&
                    uni_stream_ctx->current_object_id + backlog_threshold < cache_ctx->next_object_id)) {
                has_backlog = 1;
            } 
            if (uni_stream_ctx->current_object_id > 0 && flags != 0xff) {
                should_skip = quicrq_congestion_check_per_cnx(uni_stream_ctx->control_stream_ctx->cnx_ctx,
                    flags, has_backlog, current_time);
            }
            break;
        }
    }

    return should_skip;
}

/* Evaluation of congestion in datagram mode */
int quicrq_evaluate_datagram_congestion(quicrq_stream_ctx_t * stream_ctx, quicrq_fragment_publisher_context_t* media_ctx, uint64_t current_time)
{
    const int64_t delta_t_max = 5 * 33333;
    int has_backlog = 0;
    int should_skip = 0;

    if (media_ctx->current_fragment->object_id != 0 &&
        media_ctx->current_fragment->data_length > 0) {
        switch (media_ctx->congestion_control_mode) {
        case quicrq_congestion_control_none:
            break;
        case quicrq_congestion_control_group:
            /* TODO: compute group mode congestion control */
            break;
        case quicrq_congestion_control_delay:
        default:
            has_backlog = (current_time - media_ctx->current_fragment->cache_time) > delta_t_max;
            should_skip = quicrq_congestion_check_per_cnx(stream_ctx->cnx_ctx,
                media_ctx->current_fragment->flags, has_backlog, current_time);
            break;
        }
    }

    return should_skip;
}