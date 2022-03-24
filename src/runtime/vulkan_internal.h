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

// --

// Vulkan API version identifier macro
#define VK_MAKE_API_VERSION(variant, major, minor, patch) \
    ((((uint32_t)(variant)) << 29) | (((uint32_t)(major)) << 22) | (((uint32_t)(minor)) << 12) | ((uint32_t)(patch)))

// Vulkan API version 1.0.0
#define VK_API_VERSION_1_0 VK_MAKE_API_VERSION(0, 1, 0, 0)  // Patch version should always be set to 0

// Environment variable string delimiter
#ifdef WINDOWS
#define HL_VK_ENV_DELIM ";"
#else
#define HL_VK_ENV_DELIM ":"
#endif

// Prototypes for the subset of the Vulkan API we need
#define VK_NO_PROTOTYPES
// NOLINTNEXTLINE
#include "mini_vulkan.h"

// --

namespace Halide {
namespace Runtime {
namespace Internal {
namespace Vulkan {

// clang-format off
#define VULKAN_FN(fn) WEAK PFN_##fn fn; 
#include "vulkan_functions.h"
#undef VULKAN_FN
// clang-format on

void WEAK vk_load_vulkan_functions(VkInstance instance) {
#define VULKAN_FN(fn) fn = (PFN_##fn)vkGetInstanceProcAddr(instance, #fn);
#include "vulkan_functions.h"
#undef VULKAN_FN
}

// --

extern WEAK halide_device_interface_t vulkan_device_interface;

// --

WEAK int vk_create_memory_allocator(void *user_context);
WEAK int vk_destroy_memory_allocator(void *user_context);
WEAK VkShaderModule vk_compile_shader_module(void *user_context, VkDevice device, const VkAllocationCallbacks* allocation_callbacks, const char *src, int size);
WEAK int vk_destroy_kernel(void* user_context, VkDevice device, VkShaderModule shader_module);
WEAK const char *get_vulkan_error_name(VkResult error);

// --

// Compilation cache for compiled shader modules
WEAK Halide::Internal::GPUCompilationCache<VkDevice, VkShaderModule> compilation_cache;

// --

WEAK VkShaderModule vk_compile_shader_module(void *user_context, VkDevice device, const VkAllocationCallbacks* allocation_callbacks, const char *src, int size) {
    debug(user_context)
        << "Vulkan: vk_compile_shader_module (user_context: " << user_context
        << ", device: " << (void*)device
        << ", allocation_callbacks: " << (void*)allocation_callbacks
        << ", source: " << (void *)src
        << ", size: " << size << "\n";

#ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
#endif

    VkShaderModuleCreateInfo shader_info = {
        VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        nullptr,               // pointer to structure extending this
        0,                     // flags (curently unused)
        (size_t)size,          // code size in bytes
        (const uint32_t *)src  // source
    };

    VkShaderModule shader_module;
    VkResult result = vkCreateShaderModule(device, &shader_info, allocation_callbacks, &shader_module);
    if (result != VK_SUCCESS) {
        error(user_context) << "Vulkan: vkCreateShaderModule Failed! Error returned: " << get_vulkan_error_name(result) << "\n";
    }

#ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
#endif

    return shader_module;
}

// --

// Returns the corresponding string for a given vulkan error code
WEAK const char *get_vulkan_error_name(VkResult error) {
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

}  // namespace Vulkan
}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

#endif  // HALIDE_RUNTIME_VULKAN_INTERNAL_H
