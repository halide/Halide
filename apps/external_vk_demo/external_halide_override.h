#pragma once

#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

// External Vulkan context override functions for Halide
// These functions allow applications to provide their own Vulkan context
// instead of letting Halide create its own

int halide_vulkan_acquire_context(
    void* user_context, struct halide_vulkan_memory_allocator** allocator,
    VkInstance* instance, VkDevice* device, VkPhysicalDevice* physical_device,
    VkQueue* queue, uint32_t* queue_family_index,
    VkDebugUtilsMessengerEXT* messenger, bool create);

int halide_vulkan_release_context(void* user_context, VkInstance instance,
                                  VkDevice device, VkQueue queue,
                                  VkDebugUtilsMessengerEXT messenger);

int halide_vulkan_export_memory_allocator(
    void* user_context, struct halide_vulkan_memory_allocator* allocator);

int halide_vulkan_memory_allocator_release(
    void* user_context, struct halide_vulkan_memory_allocator* allocator,
    VkInstance instance, VkDebugUtilsMessengerEXT messenger);

// Buffer wrapping functions
int halide_vulkan_wrap_vk_buffer(void* user_context,
                                 struct halide_buffer_t* buf,
                                 uint64_t vk_buffer);

int halide_vulkan_detach_vk_buffer(void* user_context,
                                   struct halide_buffer_t* buf);

// Application interface to register Vulkan context
void register_external_vulkan_context(VkInstance instance, VkDevice device,
                                      VkPhysicalDevice physical_device,
                                      VkQueue queue,
                                      uint32_t queue_family_index);

void unregister_external_vulkan_context();

#ifdef __cplusplus
}
#endif
