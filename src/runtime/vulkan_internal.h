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
struct VulkanShaderBinding;
struct VulkanCompiledShaderModule;
struct VulkanCompilationCacheEntry;

// --------------------------------------------------------------------------

namespace {  // internalize

// --------------------------------------------------------------------------
// Memory
// --------------------------------------------------------------------------
void *vk_host_malloc(void *user_context, size_t size, size_t alignment, VkSystemAllocationScope scope, const VkAllocationCallbacks *callbacks = nullptr);
void vk_host_free(void *user_context, void *ptr, const VkAllocationCallbacks *callbacks = nullptr);
int vk_device_crop_from_offset(void *user_context, const struct halide_buffer_t *src, int64_t offset, struct halide_buffer_t *dst);
VulkanMemoryAllocator *vk_create_memory_allocator(void *user_context, VkDevice device, VkPhysicalDevice physical_device,
                                                  const VkAllocationCallbacks *alloc_callbacks);

int vk_destroy_memory_allocator(void *user_context, VulkanMemoryAllocator *allocator);
int vk_clear_device_buffer(void *user_context,
                           VulkanMemoryAllocator *allocator,
                           VkCommandBuffer command_buffer,
                           VkQueue command_queue,
                           VkBuffer device_buffer);
// --------------------------------------------------------------------------
// Context
// --------------------------------------------------------------------------

int vk_create_context(
    void *user_context,
    VulkanMemoryAllocator **allocator,
    VkInstance *instance,
    VkDevice *device,
    VkPhysicalDevice *physical_device,
    VkQueue *queue, uint32_t *queue_family_index);

int vk_destroy_context(
    void *user_context,
    VulkanMemoryAllocator *allocator,
    VkInstance instance,
    VkDevice device,
    VkPhysicalDevice physical_device,
    VkQueue queue);

int vk_find_compute_capability(void *user_context, int *major, int *minor);

int vk_create_instance(void *user_context, const StringTable &requested_layers, VkInstance *instance, const VkAllocationCallbacks *alloc_callbacks);
int vk_destroy_instance(void *user_context, VkInstance instance, const VkAllocationCallbacks *alloc_callbacks);

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
int vk_create_command_pool(void *user_context, VulkanMemoryAllocator *allocator, uint32_t queue_index, VkCommandPool *command_pool);
int vk_destroy_command_pool(void *user_context, VulkanMemoryAllocator *allocator, VkCommandPool command_pool);

// -- Command Buffer
int vk_create_command_buffer(void *user_context, VulkanMemoryAllocator *allocator, VkCommandPool pool, VkCommandBuffer *command_buffer);
int vk_destroy_command_buffer(void *user_context, VulkanMemoryAllocator *allocator, VkCommandPool command_pool, VkCommandBuffer command_buffer);

struct ScopedVulkanCommandBufferAndPool;

int vk_fill_command_buffer_with_dispatch_call(void *user_context,
                                              VkDevice device,
                                              VkCommandBuffer command_buffer,
                                              VkPipeline compute_pipeline,
                                              VkPipelineLayout pipeline_layout,
                                              VkDescriptorSet descriptor_set,
                                              uint32_t descriptor_set_index,
                                              int blocksX, int blocksY, int blocksZ);

int vk_submit_command_buffer(void *user_context, VkQueue queue, VkCommandBuffer command_buffer);

// -- Scalar Uniform Buffer
bool vk_needs_scalar_uniform_buffer(void *user_context,
                                    size_t arg_sizes[],
                                    void *args[],
                                    int8_t arg_is_buffer[]);

size_t vk_estimate_scalar_uniform_buffer_size(void *user_context,
                                              size_t arg_sizes[],
                                              void *args[],
                                              int8_t arg_is_buffer[]);

MemoryRegion *vk_create_scalar_uniform_buffer(void *user_context,
                                              VulkanMemoryAllocator *allocator,
                                              size_t scalar_buffer_size);

int vk_update_scalar_uniform_buffer(void *user_context,
                                    VulkanMemoryAllocator *allocator,
                                    MemoryRegion *region,
                                    size_t arg_sizes[],
                                    void *args[],
                                    int8_t arg_is_buffer[]);

int vk_destroy_scalar_uniform_buffer(void *user_context, VulkanMemoryAllocator *allocator,
                                     MemoryRegion *scalar_args_region);
// -- Descriptor Pool
int vk_create_descriptor_pool(void *user_context,
                              VulkanMemoryAllocator *allocator,
                              uint32_t uniform_buffer_count,
                              uint32_t storage_buffer_count,
                              VkDescriptorPool *descriptor_pool);

int vk_destroy_descriptor_pool(void *user_context,
                               VulkanMemoryAllocator *allocator,
                               VkDescriptorPool descriptor_pool);

// -- Descriptor Set Layout
uint32_t vk_count_bindings_for_descriptor_set(void *user_context,
                                              size_t arg_sizes[],
                                              void *args[],
                                              int8_t arg_is_buffer[]);

int vk_create_descriptor_set_layout(void *user_context,
                                    VulkanMemoryAllocator *allocator,
                                    uint32_t uniform_buffer_count,
                                    uint32_t storage_buffer_count,
                                    VkDescriptorSetLayout *layout);

int vk_destroy_descriptor_set_layout(void *user_context,
                                     VulkanMemoryAllocator *allocator,
                                     VkDescriptorSetLayout descriptor_set_layout);

// -- Descriptor Set
int vk_create_descriptor_set(void *user_context,
                             VulkanMemoryAllocator *allocator,
                             VkDescriptorSetLayout descriptor_set_layout,
                             VkDescriptorPool descriptor_pool,
                             VkDescriptorSet *descriptor_set);

int vk_update_descriptor_set(void *user_context,
                             VulkanMemoryAllocator *allocator,
                             VkBuffer *scalar_args_buffer,
                             size_t uniform_buffer_count,
                             size_t storage_buffer_count,
                             size_t arg_sizes[],
                             void *args[],
                             int8_t arg_is_buffer[],
                             VkDescriptorSet descriptor_set);

// -- Pipeline Layout
int vk_create_pipeline_layout(void *user_context,
                              VulkanMemoryAllocator *allocator,
                              uint32_t descriptor_set_count,
                              VkDescriptorSetLayout *descriptor_set_layouts,
                              VkPipelineLayout *pipeline_layout);

int vk_destroy_pipeline_layout(void *user_context,
                               VulkanMemoryAllocator *allocator,
                               VkPipelineLayout pipeline_layout);
// -- Compute Pipeline
int vk_create_compute_pipeline(void *user_context,
                               VulkanMemoryAllocator *allocator,
                               const char *pipeline_name,
                               VkShaderModule shader_module,
                               VkPipelineLayout pipeline_layout,
                               VkSpecializationInfo *specialization_info,
                               VkPipeline *compute_pipeline);

int vk_setup_compute_pipeline(void *user_context,
                              VulkanMemoryAllocator *allocator,
                              VulkanShaderBinding *shader_bindings,
                              VkShaderModule shader_module,
                              VkPipelineLayout pipeline_layout,
                              VkPipeline *compute_pipeline);

int vk_destroy_compute_pipeline(void *user_context,
                                VulkanMemoryAllocator *allocator,
                                VkPipeline compute_pipeline);

// -- Kernel Module
VulkanCompilationCacheEntry *vk_compile_kernel_module(void *user_context, VulkanMemoryAllocator *allocator,
                                                      const char *ptr, int size);

// -- Shader Module
VulkanShaderBinding *vk_decode_shader_bindings(void *user_context, VulkanMemoryAllocator *allocator,
                                               const uint32_t *module_ptr, uint32_t module_size);

VulkanCompiledShaderModule *vk_compile_shader_module(void *user_context, VulkanMemoryAllocator *allocator,
                                                     const char *src, int size);

int vk_destroy_shader_modules(void *user_context, VulkanMemoryAllocator *allocator);

// -- Copy Buffer
int vk_do_multidimensional_copy(void *user_context, VkCommandBuffer command_buffer,
                                const device_copy &c, uint64_t src_offset, uint64_t dst_offset,
                                int d, bool from_host, bool to_host);

// --------------------------------------------------------------------------
// Debug & Errors
// --------------------------------------------------------------------------

VkResult vk_create_debug_utils_messenger(void *user_context, VkInstance instance, VulkanMemoryAllocator *allocator, VkDebugUtilsMessengerEXT *messenger);
void vk_destroy_debug_utils_messenger(void *user_context, VkInstance instance, VulkanMemoryAllocator *allocator, VkDebugUtilsMessengerEXT messenger);

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
    case VK_ERROR_UNKNOWN:
        return "VK_ERROR_UNKNOWN";
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

#define vk_report_error(user_context, code, func) (error((user_context)) << "Vulkan: " << (func) << " returned " << vk_get_error_name((code)) << " (code: " << (code) << ") ")

// --------------------------------------------------------------------------

}  // namespace
}  // namespace Vulkan
}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

#endif  // HALIDE_RUNTIME_VULKAN_INTERNAL_H
