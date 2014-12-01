#ifndef HALIDE_HALIDERUNTIMEOPENCL_H
#define HALIDE_HALIDERUNTIMEOPENCL_H

#include "HalideRuntime.h"

#ifdef __cplusplus
extern "C" {
#endif

/** \file
 *  Routines specific to the Halide OpenCL runtime.
 */

extern const struct halide_device_interface *halide_opencl_device_interface();

/** These are forward declared here to allow clients to override the
 *  Halide OpenCL runtime. Do not call them. */
// @{
extern int halide_opencl_initialize_kernels(void *user_context, void **state_ptr,
                                               const char *src, int size);
extern int halide_opencl_run(void *user_context,
                                  void *state_ptr,
                                  const char *entry_name,
                                  int blocksX, int blocksY, int blocksZ,
                                  int threadsX, int threadsY, int threadsZ,
                                  int shared_mem_bytes,
                                  size_t arg_sizes[],
                                  void *args[],
                                  int8_t arg_is_buffer[]);
// @}

/** Set the platform name for OpenCL to use (e.g. "Intel" or
 * "NVIDIA"). The argument is copied internally. The opencl runtime
 * will select a platform that includes this as a substring. If never
 * called, Halide uses the environment variable HL_OCL_PLATFORM_NAME,
 * or defaults to the first available platform. */
extern void halide_opencl_set_platform_name(const char *n);

/** Halide calls this to get the desired OpenCL platform
 * name. Implement this yourself to use a different platform per
 * user_context. The default implementation returns the value set by
 * halide_set_ocl_platform_name, or the value of the environment
 * variable HL_OCL_PLATFORM_NAME. The output is valid until the next
 * call to halide_set_ocl_platform_name. */
extern const char *halide_opencl_get_platform_name(void *user_context);

/** Set the device type for OpenCL to use. The argument is copied
 * internally. It must be "cpu", "gpu", or "acc". If never called,
 * Halide uses the environment variable HL_OCL_DEVICE_TYPE. */
extern void halide_opencl_set_device_type(const char *n);

/** Halide calls this to gets the desired OpenCL device
 * type. Implement this yourself to use a different device type per
 * user_context. The default implementation returns the value set by
 * halide_set_ocl_device_type, or the environment variable
 * HL_OCL_DEVICE_TYPE. The result is valid until the next call to
 * halide_set_ocl_device_type. */
extern const char *halide_opencl_get_device_type(void *user_context);

#ifdef __cplusplus
} // End extern "C"
#endif

#endif // HALIDE_HALIDERUNTIMEOPENCL_H
