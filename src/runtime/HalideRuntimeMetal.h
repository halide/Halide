#ifndef HALIDE_HALIDERUNTIMEMETAL_H
#define HALIDE_HALIDERUNTIMEMETAL_H

#include "HalideRuntime.h"

#ifdef __cplusplus
extern "C" {
#endif

/** \file
 *  Routines specific to the Halide Metal runtime.
 */

#define HALIDE_RUNTIME_METAL

extern const struct halide_device_interface_t *halide_metal_device_interface();

/** These are forward declared here to allow clients to override the
 *  Halide Metal runtime. Do not call them. */
// @{
extern int halide_metal_initialize_kernels(void *user_context, void **state_ptr,
                                           const char *src, int size);

extern int halide_metal_run(void *user_context,
                            void *state_ptr,
                            const char *entry_name,
                            int blocksX, int blocksY, int blocksZ,
                            int threadsX, int threadsY, int threadsZ,
                            int shared_mem_bytes,
                            size_t arg_sizes[],
                            void *args[],
                            int8_t arg_is_buffer[],
                            int num_attributes,
                            float* vertex_buffer,
                            int num_coords_dim0,
                            int num_coords_dim1);
// @}

/** Set the underlying MTLBuffer for a halide_buffer_t. This memory should be
 * allocated using newBufferWithLength:options or similar and must
 * have an extent large enough to cover that specified by the halide_buffer_t
 * extent fields. The dev field of the halide_buffer_t must be NULL when this
 * routine is called. This call can fail due to running out of memory
 * or being passed an invalid buffer. The device and host dirty bits
 * are left unmodified. */
extern int halide_metal_wrap_buffer(void *user_context, struct halide_buffer_t *buf, uint64_t buffer);

/** Disconnect a halide_buffer_t from the memory it was previously
 * wrapped around. Should only be called for a halide_buffer_t that
 * halide_metal_wrap_buffer was previously called on. Frees any
 * storage associated with the binding of the halide_buffer_t and the
 * buffer, but does not free the MTLBuffer. The dev field of the
 * halide_buffer_t will be NULL on return.
 */
extern int halide_metal_detach_buffer(void *user_context, struct halide_buffer_t *buf);

/** Return the underlying MTLBuffer for a halide_buffer_t. This buffer must be
 * valid on an Metal device, or not have any associated device
 * memory. If there is no device memory (dev field is NULL), this
 * returns 0.
 */
extern uintptr_t halide_metal_get_buffer(void *user_context, struct halide_buffer_t *buf);

/** Returns the offset associated with the Metal Buffer allocation via device_crop. */
extern uint64_t halide_metal_get_crop_offset(void *user_context, struct halide_buffer_t *buf);

struct halide_metal_device;
struct halide_metal_command_queue;

/** This prototype is exported as applications will typically need to
 * replace it to get Halide filters to execute on the same device and
 * command queue used for other purposes. The halide_metal_device is an
 * id \<MTLDevice\> and halide_metal_command_queue is an id \<MTLCommandQueue\>.
 * No reference counting is done by Halide on these objects. They must remain
 * valid until all off the following are true:
 * - A balancing halide_metal_release_context has occurred for each
 *     halide_metal_acquire_context which returned the device/queue
 * - All Halide filters using the context information have completed
 * - All halide_buffer_t objects on the device have had
 *     halide_device_free called or have been detached via
 *     halide_metal_detach_buffer.
 * - halide_device_release has been called on the interface returned from
 *     halide_metal_device_interface(). (This releases the programs on the context.)
 */
extern int halide_metal_acquire_context(void *user_context, struct halide_metal_device **device_ret,
                                        struct halide_metal_command_queue **queue_ret, bool create);

/** This call balances each successfull halide_metal_acquire_context call.
 * If halide_metal_acquire_context is replaced, this routine must be replaced
 * as well.
 */
extern int halide_metal_release_context(void *user_context);

#ifdef __cplusplus
} // End extern "C"
#endif

#endif // HALIDE_HALIDERUNTIMEMETAL_H
