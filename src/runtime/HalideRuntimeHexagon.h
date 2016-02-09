#ifndef HALIDE_HALIDERUNTIMEHEXAGON_H
#define HALIDE_HALIDERUNTIMEHEXAGON_H

#include "HalideRuntime.h"

#ifdef __cplusplus
extern "C" {
#endif

/** \file
 *  Routines specific to the Halide Hexagon runtime.
 */

/** These are forward declared here to allow clients to override the
 *  Halide Hexagon runtime. Do not call them. */
// @{
extern int halide_hexagon_run(void *user_context,
                           const char *entry_name,
                           size_t arg_sizes[],
                           void *args[],
                           int8_t arg_is_buffer[]);
// @}

#ifdef __cplusplus
} // End extern "C"
#endif

#endif // HALIDE_HALIDERUNTIMEHEXAGON_H
