#ifndef HALIDE_HALIDERUNTIMEVULKAN_H
#define HALIDE_HALIDERUNTIMEVULKAN_H

// Don't include HalideRuntime.h if the contents of it were already pasted into a generated header above this one
#ifndef HALIDE_HALIDERUNTIME_H

#include "HalideRuntime.h"

#endif
/** \file
 *  Routines specific to the Halide Vulkan runtime.
 */

#ifdef __cplusplus
extern "C" {
#endif

#define HALIDE_RUNTIME_VULKAN

extern const struct halide_device_interface_t *halide_vulkan_device_interface();

/** These are forward declared here to allow clients to override the
 *  Halide Vulkan runtime. Do not call them. */
// @{
extern int halide_vulkan_initialize_kernels(void *user_context, void **state_ptr,
                                            const char *src, int size);

extern int halide_vulkan_run(void *user_context,
                             void *state_ptr,
                             const char *entry_name,
                             int blocksX, int blocksY, int blocksZ,
                             int threadsX, int threadsY, int threadsZ,
                             int shared_mem_bytes,
                             size_t arg_sizes[],
                             void *args[],
                             int8_t arg_is_buffer[]);

extern void halide_vulkan_finalize_kernels(void *user_context, void *state_ptr);

// @}

// The default implementation of halide_acquire_vulkan_context uses
// the global pointers above, and serializes access with a spin lock.
// Overriding implementations of acquire/release must implement the
// following behavior:

//  - halide_acquire_vulkan_context should always store a valid
//   instance/device/queue in the corresponding out parameters,
//   or return an error code.
// - A call to halide_acquire_vulkan_context is followed by a matching
//   call to halide_release_vulkan_context. halide_acquire_vulkan_context
//   should block while a previous call (if any) has not yet been
//   released via halide_release_vulkan_context.
// TODO: describe memory type index
// TODO: describe queue family index
extern int halide_vulkan_acquire_context(void *user_context, struct VkInstance_T **instance,
                                         struct VkDevice_T **device, struct VkQueue_T **queue,
                                         struct VkPhysicalDevice_T **physical_device, uint32_t *queue_family_index, bool create = true);

extern int halide_vulkan_release_context(void *user_context, struct VkInstance_T *instance, struct VkDevice_T *device, struct VkQueue_T *queue);
#ifdef __cplusplus
}  // End extern "C"
#endif

#endif  // HALIDE_HALIDERUNTIMEVULKAN_H
