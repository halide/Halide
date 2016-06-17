#ifndef HALIDE_HALIDERUNTIMEHEXAGONHOST_H
#define HALIDE_HALIDERUNTIMEHEXAGONHOST_H

#include "HalideRuntime.h"

#ifdef __cplusplus
extern "C" {
#endif

/** \file
 *  Routines specific to the Halide Hexagon host-side runtime.
 */

typedef int halide_hexagon_handle_t;

extern const struct halide_device_interface *halide_hexagon_device_interface();

/** The device handle for Hexagon is simply a pointer and size, stored
 * in the dev field of the buffer_t. If the buffer is allocated in a
 * particular way (ion_alloc), the buffer will be shared with Hexagon
 * (not copied). The dev field of the buffer_t must be NULL when this
 * routine is called. This call can fail due to running out of memory
 * or being passed an invalid device handle. The device and host
 * dirty bits are left unmodified. */
extern int halide_hexagon_wrap_device_handle(void *user_context, struct buffer_t *buf,
                                             void *ptr, size_t size);

/** Disconnect this buffer_t from the device handle it was previously
 * wrapped around. Should only be called for a buffer_t that
 * halide_hexagon_wrap_device_handle was previously called on. Frees any
 * storage associated with the binding of the buffer_t and the device
 * handle, but does not free the device handle. The previously
 * wrapped device handle is returned. The dev field of the buffer_t
 * will be NULL on return. */
extern void *halide_hexagon_detach_device_handle(void *user_context, struct buffer_t *buf);

/** Return the underlying device handle for a buffer_t. If there is
 * no device memory (dev field is NULL), this returns 0. */
extern void *halide_hexagon_get_device_handle(void *user_context, struct buffer_t *buf);
extern size_t halide_hexagon_get_device_size(void *user_context, struct buffer_t *buf);

/** These are forward declared here to allow clients to override the
 *  Halide Hexagon runtime. Do not call them. */
// @{
extern int halide_hexagon_initialize_kernels(void *user_context,
                                             void **module_ptr,
                                             const uint8_t *code, size_t code_size);
extern int halide_hexagon_run(void *user_context,
                              void *module_ptr,
                              const char *name,
                              halide_hexagon_handle_t *function,
                              size_t arg_sizes[],
                              void *args[],
                              int arg_flags[]);
extern int halide_hexagon_device_release(void* user_context);
// @}

#ifdef __cplusplus
} // End extern "C"
#endif

#endif // HALIDE_HALIDERUNTIMEHEXAGONHOST_H
