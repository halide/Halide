#ifndef HALIDE_RUNTIME_VULKAN_COMPILER_H
#define HALIDE_RUNTIME_VULKAN_COMPILER_H

#include "vulkan_internal.h"

// --------------------------------------------------------------------------

namespace Halide {
namespace Runtime {
namespace Internal {
namespace Vulkan {

// --------------------------------------------------------------------------

// Compilation cache for compiled shader modules
WEAK Halide::Internal::GPUCompilationCache<VkDevice, VkShaderModule*> compilation_cache;

// --------------------------------------------------------------------------

WEAK VkShaderModule* vk_compile_shader_module(void *user_context, VulkanMemoryAllocator* allocator, 
                                              const char *src, int size) {
    debug(user_context)
        << "Vulkan: vk_compile_shader_module (user_context: " << user_context
        << ", allocator: " << (void*)allocator
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

    VkSystemAllocationScope alloc_scope = VkSystemAllocationScope::VK_SYSTEM_ALLOCATION_SCOPE_OBJECT;
    VkShaderModule* shader_module = (VkShaderModule*)vk_host_malloc(user_context, sizeof(VkShaderModule), 0, alloc_scope, allocator->callbacks());

    VkResult result = vkCreateShaderModule(allocator->current_device(), &shader_info, allocator->callbacks(), shader_module);
    if ((result != VK_SUCCESS) || (shader_module == nullptr)) {
        error(user_context) << "Vulkan: vkCreateShaderModule Failed! Error returned: " << vk_get_error_name(result) << "\n";
        vk_host_free(user_context, shader_module, allocator->callbacks());
        return nullptr;
    }

#ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
#endif

    return shader_module;
}

WEAK int vk_destroy_shader_modules(void* user_context, VulkanMemoryAllocator* allocator){
    struct DestroyShaderModule {
        void* user_context = nullptr;
        VkDevice device = nullptr;
        const VkAllocationCallbacks* allocation_callbacks = nullptr;

        DestroyShaderModule(void* ctx, VkDevice dev, const VkAllocationCallbacks* callbacks) :
            user_context(ctx), device(dev), allocation_callbacks(callbacks) {}

        void operator()(VkShaderModule* shader_module) {
            if(shader_module != nullptr) {
                vkDestroyShaderModule(device, *shader_module, allocation_callbacks);
                vk_host_free(user_context, shader_module, allocation_callbacks);
            }
        }
    };
    
    DestroyShaderModule module_destructor(user_context, allocator->current_device(), allocator->callbacks());
    compilation_cache.delete_context(user_context, allocator->current_device(), module_destructor);
    return VK_SUCCESS;
}
    
// --------------------------------------------------------------------------

}  // namespace Vulkan
}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

#endif  // HALIDE_RUNTIME_VULKAN_COMPILER_H
