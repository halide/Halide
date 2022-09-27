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

// - halide_acquire_vulkan_context should always store a valid
//   instance/device/queue in the corresponding out parameters,
//   or return an error code.
// - A call to halide_acquire_vulkan_context is followed by a matching
//   call to halide_release_vulkan_context. halide_acquire_vulkan_context
//   should block while a previous call (if any) has not yet been
//   released via halide_release_vulkan_context.
// - Parameters:
//      allocator: an internal halide type handle used for allocating resources
//      instance: the vulkan instance handle
//      device: the vulkan device handle
//      physical_device: the vulkan physical device handle
//      command_pool: the vulkan command pool handle (strangely doesn't have a VkCommandPool_T typedef)
//      queue: the vulkan queue handle
//      queue_family_index: the index corresponding to the device queue properties for the device (as described by vkGetPhysicalDeviceQueueFamilyProperties)
//      create: if set to true, attempt to create a new vulkan context, otherwise acquire the current one
struct halide_vulkan_memory_allocator;
extern int halide_vulkan_acquire_context(void *user_context,
                                         struct halide_vulkan_memory_allocator **allocator,
                                         struct VkInstance_T **instance,
                                         struct VkDevice_T **device,
                                         struct VkPhysicalDevice_T **physical_device,
                                         uint64_t *command_pool,
                                         struct VkQueue_T **queue,
                                         uint32_t *queue_family_index,
                                         bool create = true);

extern int halide_vulkan_release_context(void *user_context,
                                         struct VkInstance_T *instance,
                                         struct VkDevice_T *device,
                                         struct VkQueue_T *queue);

// --

// Override the default allocation callbacks (default uses Vulkan runtime implementation)
extern void halide_vulkan_set_allocation_callbacks(const struct VkAllocationCallbacks *callbacks);

// Access the current allocation callbacks
// -- may return nullptr ... which indicates the default Vulkan runtime implementation is being used)
extern const struct VkAllocationCallbacks *halide_vulkan_get_allocation_callbacks(void *user_context);

// Access methods to assign/retrieve required layer names for the context
extern void halide_vulkan_set_layer_names(const char *n);
extern const char *halide_vulkan_get_layer_names(void *user_context);

// Access methods to assign/retrieve required externsion names for the context
extern void halide_vulkan_set_extension_names(const char *n);
extern const char *halide_vulkan_get_extension_names(void *user_context);

// Access methods to assign/retrieve required device type names for the context (either "cpu", "gpu" (any), "discrete-gpu" (only), "virtual-gpu" (sw))
extern void halide_vulkan_set_device_type(const char *n);
extern const char *halide_vulkan_get_device_type(void *user_context);

// Access methods to assign/retrieve specific build options to the Vulkan runtime compiler
extern void halide_vulkan_set_build_options(const char *n);
extern const char *halide_vulkan_get_build_options(void *user_context);

#ifdef __cplusplus
}  // End extern "C"
#endif

#endif  // HALIDE_HALIDERUNTIMEVULKAN_H
