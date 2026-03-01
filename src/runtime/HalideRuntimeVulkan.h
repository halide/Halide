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

// Guard against redefining handles if vulkan.h was included elsewhere
#ifndef VK_DEFINE_HANDLE

#define HALIDE_VULKAN_DEFINE_HANDLE(object) typedef struct object##_T *(object);

#ifndef HALIDE_VULKAN_USE_64_BIT_PTR_DEFINES
#if defined(__LP64__) || defined(_WIN64) || (defined(__x86_64__) && !defined(__ILP32__)) || defined(_M_X64) || defined(__ia64) || defined(_M_IA64) || defined(__aarch64__) || defined(__powerpc64__) || (defined(__riscv) && __riscv_xlen == 64)
#define HALIDE_VULKAN_USE_64_BIT_PTR_DEFINES 1
#else
#define HALIDE_VULKAN_USE_64_BIT_PTR_DEFINES 0
#endif
#endif

#ifndef HALIDE_VULKAN_DEFINE_NON_DISPATCHABLE_HANDLE
#if (HALIDE_VULKAN_USE_64_BIT_PTR_DEFINES == 1)
#define HALIDE_VULKAN_DEFINE_NON_DISPATCHABLE_HANDLE(object) typedef struct object##_T *(object);
#else
#define HALIDE_VULKAN_DEFINE_NON_DISPATCHABLE_HANDLE(object) typedef uint64_t(object);
#endif
#endif

HALIDE_VULKAN_DEFINE_HANDLE(VkInstance)
HALIDE_VULKAN_DEFINE_HANDLE(VkPhysicalDevice)
HALIDE_VULKAN_DEFINE_HANDLE(VkDevice)
HALIDE_VULKAN_DEFINE_HANDLE(VkQueue)
HALIDE_VULKAN_DEFINE_NON_DISPATCHABLE_HANDLE(VkCommandPool)
HALIDE_VULKAN_DEFINE_NON_DISPATCHABLE_HANDLE(VkDebugUtilsMessengerEXT)

#endif

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
//      queue: the vulkan queue handle
//      queue_family_index: the index corresponding to the device queue properties for the device (as described by vkGetPhysicalDeviceQueueFamilyProperties)
//      create: if set to true, attempt to create a new vulkan context, otherwise acquire the current one
struct halide_vulkan_memory_allocator;
extern int halide_vulkan_acquire_context(void *user_context,
                                         struct halide_vulkan_memory_allocator **allocator,
                                         VkInstance *instance,
                                         VkDevice *device,
                                         VkPhysicalDevice *physical_device,
                                         VkQueue *queue,
                                         uint32_t *queue_family_index,
                                         VkDebugUtilsMessengerEXT *messenger,
                                         bool create = true);

extern int halide_vulkan_release_context(void *user_context,
                                         VkInstance instance,
                                         VkDevice device,
                                         VkQueue queue,
                                         VkDebugUtilsMessengerEXT messenger);

// - halide_vulkan_export_memory_allocator
//   exports the internally allocated memory allocator in case the user wants to just set
//   up their own context but use Halide's memory allocator. Must have overridden halide_vulkan_acquire_context
//   and halide_vulkan_release_context. Must override also halide_vulkan_export_memory_allocator and guard access
//   with the same locking used by the custom acquire/release implementations. This allows the allocator to be
//   saved for future halide_vulkan_acquire_context calls that Halide will automatically issue to retrieve
//   the custom context.
extern int halide_vulkan_export_memory_allocator(void *user_context,
                                                 struct halide_vulkan_memory_allocator *allocator);
// - halide_vulkan_memory_allocator_release
//   releases the internally allocated memory allocator, important for proper memory cleanup. Must have overridden halide_vulkan_acquire_context
//   and halide_vulkan_release_context, and must coordinate with the same locking as the custom implementations.
extern int halide_vulkan_memory_allocator_release(void *user_context,
                                                  struct halide_vulkan_memory_allocator *allocator,
                                                  VkInstance instance,
                                                  VkDebugUtilsMessengerEXT messenger);
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
