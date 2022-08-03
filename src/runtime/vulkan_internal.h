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

// Declarations
class VulkanMemoryAllocator;

// --------------------------------------------------------------------------

namespace { // internalize 

// --------------------------------------------------------------------------
// Memory
// --------------------------------------------------------------------------
void *vk_host_malloc(void *user_context, size_t size, size_t alignment, VkSystemAllocationScope scope, const VkAllocationCallbacks *callbacks = nullptr);
void vk_host_free(void *user_context, void *ptr, const VkAllocationCallbacks *callbacks = nullptr);

VulkanMemoryAllocator *vk_create_memory_allocator(void *user_context, VkDevice device, VkPhysicalDevice physical_device,
                                                       const VkAllocationCallbacks *alloc_callbacks);

int vk_destroy_memory_allocator(void *user_context, VulkanMemoryAllocator *allocator);

// --------------------------------------------------------------------------
// Context
// --------------------------------------------------------------------------
int vk_create_context(
    void *user_context,
    VulkanMemoryAllocator **allocator,
    VkInstance *instance,
    VkDevice *device, 
    VkPhysicalDevice *physical_device,
    VkCommandPool *command_pool,
    VkQueue *queue, uint32_t *queue_family_index);

int vk_create_instance(void *user_context, const StringTable &requested_layers, VkInstance *instance, const VkAllocationCallbacks *alloc_callbacks);

int vk_select_device_for_context(void *user_context,
                                      VkInstance *instance, VkDevice *device,
                                      VkPhysicalDevice *physical_device,
                                      uint32_t *queue_family_index);

int vk_create_device(void *user_context, const StringTable &requested_layers, VkInstance *instance, VkDevice *device, VkQueue *queue,
                          VkPhysicalDevice *physical_device, uint32_t *queue_family_index, const VkAllocationCallbacks *alloc_callbacks);

// --------------------------------------------------------------------------
// Extensions
// --------------------------------------------------------------------------
uint32_t vk_get_requested_layers(void *user_context, StringTable &layer_table);
uint32_t vk_get_required_instance_extensions(void *user_context, StringTable &ext_table);
uint32_t vk_get_supported_instance_extensions(void *user_context, StringTable &ext_table);
uint32_t vk_get_required_device_extensions(void *user_context, StringTable &ext_table);
uint32_t vk_get_optional_device_extensions(void *user_context, StringTable &ext_table);
uint32_t vk_get_supported_device_extensions(void *user_context, VkPhysicalDevice physical_device, StringTable &ext_table);
bool vk_validate_required_extension_support(void *user_context,
                                                 const StringTable &required_extensions,
                                                 const StringTable &supported_extensions);

// --------------------------------------------------------------------------
// Resources
// --------------------------------------------------------------------------

// -- Command Pool
VkResult vk_create_command_pool(void* user_context, VulkanMemoryAllocator* allocator, uint32_t queue_index, VkCommandPool *command_pool);
VkResult vk_destroy_command_pool(void* user_context, VulkanMemoryAllocator* allocator, VkCommandPool command_pool);

// -- Command Buffer
VkResult vk_create_command_buffer(void* user_context, VulkanMemoryAllocator* allocator,  VkCommandPool pool, VkCommandBuffer *command_buffer);

VkResult vk_fill_command_buffer_with_dispatch_call(void *user_context,
                                                        VkDevice device,
                                                        VkCommandBuffer command_buffer,
                                                        VkPipeline compute_pipeline,
                                                        VkPipelineLayout pipeline_layout,
                                                        VkDescriptorSet descriptor_set,
                                                        int blocksX, int blocksY, int blocksZ);

VkResult vk_submit_command_buffer(void *user_context, VkQueue queue, VkCommandBuffer command_buffer);

// -- Scalar Uniform Buffer
size_t vk_estimate_scalar_uniform_buffer_size(void *user_context,
                                                   size_t arg_sizes[],
                                                   void *args[],
                                                   int8_t arg_is_buffer[]);

MemoryRegion *vk_create_scalar_uniform_buffer(void *user_context,
                                                   VulkanMemoryAllocator *allocator,
                                                   size_t arg_sizes[],
                                                   void *args[],
                                                   int8_t arg_is_buffer[]);

void vk_destroy_scalar_uniform_buffer(void *user_context, VulkanMemoryAllocator *allocator,
                                           MemoryRegion *scalar_args_region);
// -- Descriptor Pool
VkResult vk_create_descriptor_pool(void *user_context,
                                   VulkanMemoryAllocator *allocator,
                                   uint32_t storage_buffer_count,
                                   VkDescriptorPool *descriptor_pool);

VkResult vk_destroy_descriptor_pool(void* user_context, 
                                    VulkanMemoryAllocator *allocator,
                                    VkDescriptorPool descriptor_pool);

// -- Descriptor Set Layout
uint32_t vk_count_bindings_for_descriptor_set(void *user_context,
                                              size_t arg_sizes[],
                                              void *args[],
                                              int8_t arg_is_buffer[]);

VkResult vk_create_descriptor_set_layout(void *user_context,
                                              VkDevice device,
                                              size_t arg_sizes[],
                                              void *args[],
                                              int8_t arg_is_buffer[],
                                              VkDescriptorSetLayout *layout);

VkResult vk_destroy_descriptor_set_layout(void* user_context, 
                                          VulkanMemoryAllocator *allocator,
                                          VkDescriptorSetLayout descriptor_set_layout);

// -- Descriptor Set
VkResult vk_create_descriptor_set(void *user_context,
                                  VulkanMemoryAllocator *allocator,
                                  VkDescriptorSetLayout descriptor_set_layout,
                                  VkDescriptorPool descriptor_pool,
                                  VkDescriptorSet *descriptor_set);

VkResult vk_update_descriptor_set(void *user_context,
                                  VulkanMemoryAllocator *allocator,
                                  VkBuffer scalar_args_buffer,
                                  size_t storage_buffer_count,
                                  size_t arg_sizes[],
                                  void *args[],
                                  int8_t arg_is_buffer[],
                                  VkDescriptorSet descriptor_set);

// -- Pipeline Layout
VkResult vk_create_pipeline_layout(void *user_context,
                                   VulkanMemoryAllocator *allocator,
                                   VkDescriptorSetLayout *descriptor_set_layout,
                                   VkPipelineLayout *pipeline_layout);

VkResult vk_destroy_pipeline_layout(void* user_context, 
                                    VulkanMemoryAllocator *allocator,
                                    VkPipelineLayout pipeline_layout);
// -- Compute Pipeline
VkResult vk_create_compute_pipeline(void *user_context,
                                    VulkanMemoryAllocator *allocator,
                                    const char *pipeline_name,
                                    VkShaderModule shader_module,
                                    VkPipelineLayout pipeline_layout,
                                    VkPipeline *compute_pipeline);

VkResult vk_destroy_compute_pipeline(void* user_context, 
                                     VulkanMemoryAllocator *allocator,
                                     VkPipeline compute_pipeline);

// -- Shader Module
VkShaderModule *vk_compile_shader_module(void *user_context, VulkanMemoryAllocator *allocator,
                                         const char *src, int size);

int vk_destroy_shader_modules(void *user_context, VulkanMemoryAllocator *allocator);

// -- Copy Buffer
int vk_do_multidimensional_copy(void *user_context, VkCommandBuffer command_buffer,
                             const device_copy &c, uint64_t src_offset, uint64_t dst_offset, int d);

// --------------------------------------------------------------------------
// Errors
// --------------------------------------------------------------------------

// Returns the corresponding string for a given vulkan error code
const char *vk_get_error_name(VkResult error) {
    switch (error) {
    case VK_SUCCESS:
        return "VK_SUCCESS";
    case VK_NOT_READY:
        return "VK_NOT_READY";
    case VK_TIMEOUT:
        return "VK_TIMEOUT";
    case VK_EVENT_SET:
        return "VK_EVENT_SET";
    case VK_EVENT_RESET:
        return "VK_EVENT_RESET";
    case VK_INCOMPLETE:
        return "VK_INCOMPLETE";
    case VK_ERROR_OUT_OF_HOST_MEMORY:
        return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY:
        return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED:
        return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST:
        return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_MEMORY_MAP_FAILED:
        return "VK_ERROR_MEMORY_MAP_FAILED";
    case VK_ERROR_LAYER_NOT_PRESENT:
        return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT:
        return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT:
        return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER:
        return "VK_ERROR_INCOMPATIBLE_DRIVER";
    case VK_ERROR_TOO_MANY_OBJECTS:
        return "VK_ERROR_TOO_MANY_OBJECTS";
    case VK_ERROR_FORMAT_NOT_SUPPORTED:
        return "VK_ERROR_FORMAT_NOT_SUPPORTED";
    case VK_ERROR_FRAGMENTED_POOL:
        return "VK_ERROR_FRAGMENTED_POOL";
    case VK_ERROR_SURFACE_LOST_KHR:
        return "VK_ERROR_SURFACE_LOST_KHR";
    case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:
        return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
    case VK_SUBOPTIMAL_KHR:
        return "VK_SUBOPTIMAL_KHR";
    case VK_ERROR_OUT_OF_DATE_KHR:
        return "VK_ERROR_OUT_OF_DATE_KHR";
    case VK_ERROR_INCOMPATIBLE_DISPLAY_KHR:
        return "VK_ERROR_INCOMPATIBLE_DISPLAY_KHR";
    case VK_ERROR_VALIDATION_FAILED_EXT:
        return "VK_ERROR_VALIDATION_FAILED_EXT";
    case VK_ERROR_INVALID_SHADER_NV:
        return "VK_ERROR_INVALID_SHADER_NV";
    case VK_ERROR_OUT_OF_POOL_MEMORY_KHR:
        return "VK_ERROR_OUT_OF_POOL_MEMORY_KHR";
    case VK_ERROR_INVALID_EXTERNAL_HANDLE_KHR:
        return "VK_ERROR_INVALID_EXTERNAL_HANDLE_KHR";
    default:
        return "<Unknown Vulkan Result Code>";
    }
}

// --------------------------------------------------------------------------

}  // namespace: (anonymous)
}  // namespace: Vulkan
}  // namespace: Internal
}  // namespace: Runtime
}  // namespace: Halide

#endif  // HALIDE_RUNTIME_VULKAN_INTERNAL_H
