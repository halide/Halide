#ifndef HALIDE_HALIDERUNTIMEOPENCL_H
#define HALIDE_HALIDERUNTIMEOPENCL_H

#include "HalideRuntime.h"

#ifdef __cplusplus
extern "C" {
#endif

/** \file
 *  Routines specific to the Halide OpenCL runtime.
 */

extern WEAK struct halide_device_interface *halide_opencl;

/** These are forward declared here to allow clients to override the
 *  Halide OpenCL runtime. Do not call them. */
// @{
extern WEAK int halide_opencl_initialize_kernels(void *user_context, void **state_ptr,
                                               const char *src, int size);
extern WEAK int halide_opencl_run(void *user_context,
                                  void *state_ptr,
                                  const char *entry_name,
                                  int blocksX, int blocksY, int blocksZ,
                                  int threadsX, int threadsY, int threadsZ,
                                  int shared_mem_bytes,
                                  size_t arg_sizes[],
                                  void *args[],
                                  int8_t arg_is_buffer[]);
// @}

#ifdef __cplusplus
} // End extern "C"
#endif

#endif // HALIDE_HALIDERUNTIMEOPENCL_H
