#ifndef HALIDE_HALIDERUNTIMEQURT_H
#define HALIDE_HALIDERUNTIMEQURT_H

#include "HalideRuntime.h"

#ifdef __cplusplus
extern "C" {
#endif

/** \file
 *  Routines specific to the Halide QuRT runtime.
 */

/** Lock and unlock an HVX context of the specified width (64 or 128
 * bytes). A successful call to hvx_lock must be followed by a call to
 * hvx_unlock. */
// @{
extern int halide_qurt_hvx_lock(void *user_context, int size);
extern int halide_qurt_hvx_unlock(void *user_context);
extern void halide_qurt_hvx_unlock_as_destructor(void *user_context, void * /*obj*/);
// @}

#ifdef __cplusplus
} // End extern "C"
#endif

#endif // HALIDE_HALIDERUNTIMEQURT_H
