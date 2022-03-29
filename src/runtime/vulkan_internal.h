#ifndef HALIDE_RUNTIME_VULKAN_INTERNAL_H
#define HALIDE_RUNTIME_VULKAN_INTERNAL_H

#include "gpu_context_common.h"
#include "printer.h"
#include "runtime_internal.h"
#include "scoped_spin_lock.h"

#include "internal/block_storage.h"
#include "internal/linked_list.h"
#include "internal/memory_arena.h"
#include "internal/string_storage.h"
#include "internal/string_table.h"

#include "vulkan_interface.h"

// --

namespace Halide {
namespace Runtime {
namespace Internal {
namespace Vulkan {

// --------------------------------------------------------------------------
// Memory 
// --------------------------------------------------------------------------
WEAK void* vk_host_malloc(void *user_context, size_t size, size_t alignment, VkSystemAllocationScope scope, const VkAllocationCallbacks* callbacks=nullptr);
WEAK void vk_host_free(void *user_context, void *ptr, const VkAllocationCallbacks* callbacks=nullptr);
WEAK int vk_create_memory_allocator(void *user_context, VkDevice device, VkPhysicalDevice physical_device, const VkAllocationCallbacks* callbacks=nullptr);
WEAK int vk_destroy_memory_allocator(void *user_context, VkDevice device, VkPhysicalDevice physical_device);
WEAK VkResult vk_allocate_device_memory(VkPhysicalDevice physical_device,
                                        VkDevice device, VkDeviceSize size,
                                        VkMemoryPropertyFlags flags,
                                        const VkAllocationCallbacks *allocator,
                                        VkDeviceMemory *device_memory);

// --------------------------------------------------------------------------
// Context
// --------------------------------------------------------------------------
WEAK int vk_create_context(
    void *user_context,
    VkInstance *instance,
    VkDevice *device, VkQueue *queue,
    VkPhysicalDevice *physical_device,
    uint32_t *queue_family_index);

WEAK int vk_create_instance(void *user_context, const StringTable &requested_layers, VkInstance *instance, const VkAllocationCallbacks* alloc_callbacks);

WEAK int vk_select_device_for_context(void *user_context,
                                      VkInstance *instance, VkDevice *device,
                                      VkPhysicalDevice *physical_device,
                                      uint32_t *queue_family_index);

WEAK int vk_create_device(void *user_context, const StringTable &requested_layers, VkInstance *instance, VkDevice *device, VkQueue *queue,
                          VkPhysicalDevice *physical_device, uint32_t *queue_family_index, const VkAllocationCallbacks* alloc_callbacks);

WEAK int vk_create_context(void *user_context, VkInstance *instance, VkDevice *device, VkQueue *queue,
                           VkPhysicalDevice *physical_device, uint32_t *queue_family_index);


// --------------------------------------------------------------------------
// Extensions
// --------------------------------------------------------------------------
WEAK uint32_t vk_get_requested_layers(void *user_context, StringTable &layer_table);
WEAK uint32_t vk_get_required_instance_extensions(void *user_context, StringTable &ext_table);
WEAK uint32_t vk_get_supported_instance_extensions(void *user_context, StringTable &ext_table);
WEAK uint32_t vk_get_required_device_extensions(void *user_context, StringTable &ext_table);
WEAK uint32_t vk_get_optional_device_extensions(void *user_context, StringTable &ext_table);
WEAK uint32_t vk_get_supported_device_extensions(void *user_context, VkPhysicalDevice physical_device, StringTable &ext_table);
WEAK bool vk_validate_required_extension_support(void *user_context,
                                                 const StringTable &required_extensions,
                                                 const StringTable &supported_extensions);

// --------------------------------------------------------------------------
// Shader Module 
// --------------------------------------------------------------------------
WEAK VkShaderModule* vk_compile_shader_module(
    void *user_context, VkDevice device, const char *src, int size,
    const VkAllocationCallbacks* allocation_callbacks);

WEAK int vk_destroy_shader_modules(
    void* user_context, VkDevice device, 
    const VkAllocationCallbacks* callbacks);
    
// --------------------------------------------------------------------------
// Errors
// --------------------------------------------------------------------------
WEAK const char *vk_get_error_name(VkResult error);

// --------------------------------------------------------------------------

}  // namespace Vulkan
}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

#endif  // HALIDE_RUNTIME_VULKAN_INTERNAL_H
