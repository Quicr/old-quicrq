#ifndef QUICRQ_REASSEMBLY_H
#define QUICRQ_REASSEMBLY_H

#include "quicrq.h"
#include "quicrq_internal.h"
#include "picoquic_utils.h"
#include "picosplay.h"


#ifdef __cplusplus
extern "C" {
#endif

 /* Handling of frame reassembly
  * Manage a list of frames being reassembled. The list is organized as a splay,
  * indexed by the frame id and frame offset. When a new segment is received
  * the code will check whether the frame is already present, and then whether the
  * segment for that frame has already arrived.
  */

typedef struct st_quicrq_reassembly_context_t {
    picosplay_tree_t frame_tree;
    uint64_t next_frame_id;
    uint64_t final_frame_id;
    int is_finished : 1;
} quicrq_reassembly_context_t;

typedef enum {
    quicrq_reassembly_frame_in_sequence,
    quicrq_reassembly_frame_peek,
    quicrq_reassembly_frame_repair
} quicrq_reassembly_frame_mode_enum;

typedef int (*quicrq_reassembly_frame_ready_fn)(
    void* media_ctx,
    uint64_t current_time,
    uint64_t frame_id,
    const uint8_t* data,
    size_t data_length,
    quicrq_reassembly_frame_mode_enum frame_mode);

/* Submit a received packet for reassembly.
 * For each reassembled frame, the function will call ()
 */
int quicrq_reassembly_input(
    quicrq_reassembly_context_t* reassembly_ctx,
    uint64_t current_time,
    const uint8_t* data,
    uint64_t frame_id,
    uint64_t offset,
    int is_last_segment,
    size_t data_length,
    quicrq_reassembly_frame_ready_fn ready_fn,
    void * app_media_ctx);

/* Obtain the final frame ID */
int quicrq_reassembly_learn_final_frame_id(
    quicrq_reassembly_context_t* reassembly_ctx,
    uint64_t final_frame_id);

/* Find the frame number of the last reassembled frame */
uint64_t quicrq_reassembly_frame_id_last(quicrq_reassembly_context_t* reassembly_ctx);

/* Initialize the reassembly context, supposedly zero on input.
 */
void quicrq_reassembly_init(quicrq_reassembly_context_t* reassembly_ctx);

/* Free the reassembly context
 */
void quicrq_reassembly_release(quicrq_reassembly_context_t* reassembly_ctx);

#ifdef __cplusplus
}
#endif

#endif /* QUICRQ_REASSEMBLY_H */