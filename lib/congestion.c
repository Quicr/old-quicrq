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


/* Handle congestion. This should be done per connection, at least once per RTT.
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

int quicrq_compute_group_mode_backlog(quicrq_fragment_cache_t* cache_ctx, uint64_t current_group_id, uint64_t current_object_id)
{
    int has_backlog = 0;

    if (current_group_id < cache_ctx->next_group_id) {
        if (current_group_id + 1 == cache_ctx->next_group_id) {

        }
    }
    return has_backlog;
}

/* Evaluation of backlog for single stream transmission
 */
int quicrq_fragment_evaluate_backlog(quicrq_fragment_publisher_context_t* media_ctx)
{
    int has_backlog = 0;
    const uint64_t backlog_threshold = 5;

    if (media_ctx->current_offset > 0 || media_ctx->length_sent > 0) {
        has_backlog = media_ctx->has_backlog;
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
            if (media_ctx->current_group_id < media_ctx->cache_ctx->next_group_id ||
                (media_ctx->current_group_id == media_ctx->cache_ctx->next_group_id &&
                    media_ctx->current_object_id + backlog_threshold < media_ctx->cache_ctx->next_object_id)) {
                has_backlog = 1;
                media_ctx->has_backlog = 1;
            }
            else {
                has_backlog = 0;
                media_ctx->has_backlog = 0;
            }
            break;
        }
    }
    return has_backlog;
}

/* Checking congestion in warp mode */
int quicrq_evaluate_warp_backlog(quicrq_uni_stream_ctx_t* uni_stream_ctx, quicrq_fragment_publisher_context_t* media_ctx)
{
    int has_backlog = 0;
    const uint64_t backlog_threshold = 5;
    quicrq_fragment_cache_t* cache_ctx = media_ctx->cache_ctx;

    switch (media_ctx->congestion_control_mode) {
    case quicrq_congestion_control_none:
        break;
    case quicrq_congestion_control_group:
        /* TODO: compute group mode congestion control */
        break;
    case quicrq_congestion_control_delay:
    default:
        if (uni_stream_ctx->current_group_id < cache_ctx->next_group_id ||
            (uni_stream_ctx->current_group_id == cache_ctx->next_group_id &&
                uni_stream_ctx->current_object_id + backlog_threshold < cache_ctx->next_object_id)) {
            has_backlog = 1;
        }
        break;
    }

    return has_backlog;
}

/* Backlog evaluation in datagram mode */
int quicrq_evaluate_datagram_backlog(quicrq_fragment_publisher_context_t* media_ctx, uint64_t current_time)
{
    const int64_t delta_t_max = 5 * 33333;
    int has_backlog = 0;

    switch (media_ctx->congestion_control_mode) {
    case quicrq_congestion_control_none:
        break;
    case quicrq_congestion_control_group:
        /* TODO: compute group mode congestion control */
        break;
    case quicrq_congestion_control_delay:
    default:
        has_backlog = (current_time - media_ctx->current_fragment->cache_time) > delta_t_max;
        break;
    }

    return has_backlog;
}