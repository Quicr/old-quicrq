/* Handling of a media segment.
 * Segments are identified by a unique name, which can be used to retrieve them.
 * Segments are created by producers, may be stored to files for later replay.
 * Segments are composed of frames. Frames have a sequence number and a timestamp.
 */

#include "quicrq.h"
#include "quicrq_internal.h"

/* Create segment in context, specifying a segment name */
/* Store a frame with number and timestamp */
/* Get a frame by number or by timestamp */