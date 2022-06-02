#ifndef HALIDE_HALIDERUNTIMEWEBGPU_H
#define HALIDE_HALIDERUNTIMEWEBGPU_H

// Don't include HalideRuntime.h if the contents of it were already pasted into a generated header above this one
#ifndef HALIDE_HALIDERUNTIME_H

#include "HalideRuntime.h"

#endif

#ifdef __cplusplus
extern "C" {
#endif

/** \file
 *  Routines specific to the Halide WebGPU runtime.
 */

#define HALIDE_RUNTIME_WEBGPU

extern const struct halide_device_interface_t *halide_webgpu_device_interface();

/** These are forward declared here to allow clients to override the
 *  Halide WebGPU runtime. Do not call them. */
// @{
extern int halide_webgpu_initialize_kernels(void *user_context, void **state_ptr,
                                            const char *src, int size);
extern int halide_webgpu_run(void *user_context,
                             void *state_ptr,
                             const char *entry_name,
                             int blocksX, int blocksY, int blocksZ,
                             int threadsX, int threadsY, int threadsZ,
                             int shared_mem_bytes,
                             halide_type_t arg_types[],
                             void *args[],
                             int8_t arg_is_buffer[]);
extern void halide_webgpu_finalize_kernels(void *user_context, void *state_ptr);
// @}

#ifdef __cplusplus
}  // End extern "C"
#endif

#endif  // HALIDE_HALIDERUNTIMEWEBGPU_H
