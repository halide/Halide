#ifndef HALIDE_HALIDERUNTIMEXRT_H
#define HALIDE_HALIDERUNTIMEXRT_H

// Don't include HalideRuntime.h if the contents of it were already pasted into a generated header above this one
#ifndef HALIDE_HALIDERUNTIME_H

#include "HalideRuntime.h"

#endif

#ifdef __cplusplus
extern "C" {
#endif

/** \file
 *  Routines specific to the Halide XRT runtime.
 */

#define HALIDE_RUNTIME_XRT

extern const struct halide_device_interface_t *halide_xrt_device_interface();

/** These are forward declared here to allow clients to override the
 *  Halide XRT runtime. Do not call them. */
// @{
extern int halide_xrt_initialize_kernels(void *user_context, void **state_ptr,
                                         const char *kernel_name);
extern int halide_xrt_run(void *user_context,
                          void *state_ptr,
                          const char *entry_name,
                          halide_type_t arg_types[],
                          void *args[],
                          int8_t arg_is_buffer[]);
extern void halide_xrt_finalize_kernels(void *user_context, void *state_ptr);
// @}

#ifdef __cplusplus
}  // End extern "C"
#endif

#endif  // HALIDE_HALIDERUNTIMEXRT_H
