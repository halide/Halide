#ifndef HALIDE_RUNTIME_VULKAN_RESOURCES_H
#define HALIDE_RUNTIME_VULKAN_RESOURCES_H

#include "vulkan_internal.h"
#include "vulkan_memory.h"

// --------------------------------------------------------------------------

namespace Halide {
namespace Runtime {
namespace Internal {
namespace Vulkan {

// Compilation cache for compiled shader modules
struct VulkanEntryPointData {
    const char *entry_point_name = nullptr;
    VkDescriptorPool descriptor_pool = {0};
    VkDescriptorSet descriptor_set = {0};
    VkPipeline compute_pipeline = {0};
    uint32_t uniform_buffer_count = 0;
    uint32_t storage_buffer_count = 0;
    uint32_t bindings_count = 0;
    MemoryRegion *args_region = nullptr;
};

struct VulkanCompilationCacheEntry {
    VkShaderModule shader_module = {0};
    VkDescriptorSetLayout *descriptor_set_layouts = nullptr;
    VkPipelineLayout pipeline_layout = {0};
    uint32_t entry_point_count = 0;
    VulkanEntryPointData *entry_point_data = nullptr;
};

WEAK Halide::Internal::GPUCompilationCache<VkDevice, VulkanCompilationCacheEntry *> compilation_cache;

// --------------------------------------------------------------------------

namespace {  // internalize

// --------------------------------------------------------------------------

VkResult vk_create_command_pool(void *user_context, VulkanMemoryAllocator *allocator, uint32_t queue_index, VkCommandPool *command_pool) {
#ifdef DEBUG_RUNTIME
    debug(user_context)
        << " vk_create_command_pool (user_context: " << user_context << ", "
        << "allocator: " << (void *)allocator << ", "
        << "device: " << (void *)allocator->current_device() << ", "
        << "queue_index: " << queue_index << ")\n";
#endif

    VkCommandPoolCreateInfo command_pool_info =
        {
            VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,      // struct type
            nullptr,                                         // pointer to struct extending this
            VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,            // flags. Assume transient short-lived single-use command buffers
            queue_index                                      // queue family index corresponding to the compute command queue
        };
    return vkCreateCommandPool(allocator->current_device(), &command_pool_info, allocator->callbacks(), command_pool);
}

void vk_destroy_command_pool(void *user_context, VulkanMemoryAllocator *allocator, VkCommandPool command_pool) {
#ifdef DEBUG_RUNTIME
    debug(user_context)
        << " vk_destroy_command_pool (user_context: " << user_context << ", "
        << "allocator: " << (void *)allocator << ", "
        << "device: " << (void *)allocator->current_device() << ", "
        << "command_pool: " << (void *)command_pool << ")\n";
#endif
    vkDestroyCommandPool(allocator->current_device(), command_pool, allocator->callbacks());
}

// --

VkResult vk_create_command_buffer(void *user_context, VulkanMemoryAllocator *allocator, VkCommandPool command_pool, VkCommandBuffer *command_buffer) {
#ifdef DEBUG_RUNTIME
    debug(user_context)
        << " vk_create_command_buffer (user_context: " << user_context << ", "
        << "allocator: " << (void *)allocator << ", "
        << "device: " << (void *)allocator->current_device() << ", "
        << "command_pool: " << (void *)command_pool << ")\n";
#endif
    VkCommandBufferAllocateInfo command_buffer_info =
        {
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,  // struct type
            nullptr,                                         // pointer to struct extending this
            command_pool,                                    // command pool for allocation
            VK_COMMAND_BUFFER_LEVEL_PRIMARY,                 // command buffer level
            1                                                // number to allocate
        };

    return vkAllocateCommandBuffers(allocator->current_device(), &command_buffer_info, command_buffer);
}

void vk_destroy_command_buffer(void *user_context, VulkanMemoryAllocator *allocator, VkCommandPool command_pool, VkCommandBuffer command_buffer) {
#ifdef DEBUG_RUNTIME
    debug(user_context)
        << " vk_destroy_command_buffer (user_context: " << user_context << ", "
        << "allocator: " << (void *)allocator << ", "
        << "device: " << (void *)allocator->current_device() << ", "
        << "command_pool: " << (void *)command_pool << ", "
        << "command_buffer: " << (void *)command_buffer << ")\n";
#endif
    vkFreeCommandBuffers(allocator->current_device(), command_pool, 1, &command_buffer);
}

VkResult vk_fill_command_buffer_with_dispatch_call(void *user_context,
                                                   VkDevice device,
                                                   VkCommandBuffer command_buffer,
                                                   VkPipeline compute_pipeline,
                                                   VkPipelineLayout pipeline_layout,
                                                   VkDescriptorSet descriptor_set,
                                                   uint32_t descriptor_set_index,
                                                   int blocksX, int blocksY, int blocksZ) {

#ifdef DEBUG_RUNTIME
    debug(user_context)
        << " vk_fill_command_buffer_with_dispatch_call (user_context: " << user_context << ", "
        << "device: " << (void *)device << ", "
        << "command_buffer: " << (void *)command_buffer << ", "
        << "pipeline_layout: " << (void *)pipeline_layout << ", "
        << "descriptor_set: " << (void *)descriptor_set << ", "
        << "descriptor_set_index: " << descriptor_set_index << ", "
        << "blocks: " << blocksX << ", " << blocksY << ", " << blocksZ << ")\n";
#endif

    VkCommandBufferBeginInfo command_buffer_begin_info = {
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,  // struct type
        nullptr,                                      // pointer to struct extending this
        VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,  // flags
        nullptr                                       // pointer to parent command buffer
    };

    VkResult result = vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info);
    if (result != VK_SUCCESS) {
        error(user_context) << "vkBeginCommandBuffer returned " << vk_get_error_name(result) << "\n";
        return result;
    }

    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute_pipeline);
    vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout,
                            descriptor_set_index, 1, &descriptor_set, 0, nullptr);
    vkCmdDispatch(command_buffer, blocksX, blocksY, blocksZ);  // TODO: make sure this is right!

    result = vkEndCommandBuffer(command_buffer);
    if (result != VK_SUCCESS) {
        error(user_context) << "vkEndCommandBuffer returned " << vk_get_error_name(result) << "\n";
        return result;
    }

    return VK_SUCCESS;
}

VkResult vk_submit_command_buffer(void *user_context, VkQueue queue, VkCommandBuffer command_buffer) {
#ifdef DEBUG_RUNTIME
    debug(user_context)
        << " vk_submit_command_buffer (user_context: " << user_context << ", "
        << "queue: " << (void *)queue << ", "
        << "command_buffer: " << (void *)command_buffer << ")\n";
#endif

    VkSubmitInfo submit_info =
        {
            VK_STRUCTURE_TYPE_SUBMIT_INFO,  // struct type
            nullptr,                        // pointer to struct extending this
            0,                              // wait semaphore count
            nullptr,                        // semaphores
            nullptr,                        // pipeline stages where semaphore waits occur
            1,                              // how many command buffers to execute
            &command_buffer,                // the command buffers
            0,                              // number of semaphores to signal
            nullptr                         // the semaphores to signal
        };

    VkResult result = vkQueueSubmit(queue, 1, &submit_info, 0);
    if (result != VK_SUCCESS) {
        debug(user_context) << "vkQueueSubmit returned " << vk_get_error_name(result) << "\n";
        return result;
    }
    return VK_SUCCESS;
}

// --

bool vk_needs_scalar_uniform_buffer(void *user_context,
                                    size_t arg_sizes[],
                                    void *args[],
                                    int8_t arg_is_buffer[]) {
    int i = 0;
    while (arg_sizes[i] > 0) {
        if (!arg_is_buffer[i]) {
            return true;
        }
        i++;
    }
    return false;
}

uint32_t vk_count_bindings_for_descriptor_set(void *user_context,
                                              size_t arg_sizes[],
                                              void *args[],
                                              int8_t arg_is_buffer[]) {

    // first binding is for passing scalar parameters in a buffer (if necessary)
    uint32_t bindings_count = vk_needs_scalar_uniform_buffer(user_context, arg_sizes, args, arg_is_buffer);

    int i = 0;
    while (arg_sizes[i] > 0) {
        if (arg_is_buffer[i]) {
            bindings_count++;
        }
        i++;
    }
    return bindings_count;
}

// --

VkResult vk_create_descriptor_pool(void *user_context,
                                   VulkanMemoryAllocator *allocator,
                                   uint32_t uniform_buffer_count,
                                   uint32_t storage_buffer_count,
                                   VkDescriptorPool *descriptor_pool) {
#ifdef DEBUG_RUNTIME
    debug(user_context)
        << " vk_create_descriptor_pool (user_context: " << user_context << ", "
        << "allocator: " << (void *)allocator << ", "
        << "device: " << (void *)allocator->current_device() << ", "
        << "uniform_buffer_count: " << (uint32_t)uniform_buffer_count << ", "
        << "storage_buffer_count: " << (uint32_t)storage_buffer_count << ")\n";
#endif

    BlockStorage::Config pool_config;
    pool_config.entry_size = sizeof(VkDescriptorPoolSize);
    pool_config.minimum_capacity = (uniform_buffer_count ? 1 : 0) + (storage_buffer_count ? 1 : 0);
    BlockStorage pool_sizes(user_context, pool_config);

    // First binding is reserved for passing scalar parameters as a uniform buffer
    if (uniform_buffer_count > 0) {
        VkDescriptorPoolSize uniform_buffer_size = {
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,  // descriptor type
            uniform_buffer_count                // all kernel args are packed into uniform buffers
        };
        pool_sizes.append(user_context, &uniform_buffer_size);
    }

    if (storage_buffer_count > 0) {
        VkDescriptorPoolSize storage_buffer_size = {
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,  // descriptor type
            storage_buffer_count                // all halide buffers are passed as storage buffers
        };
        pool_sizes.append(user_context, &storage_buffer_size);
    }

    VkDescriptorPoolCreateInfo descriptor_pool_info = {
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,   // struct type
        nullptr,                                         // point to struct extending this
        0,                                               // flags
        1,                                               // this pool will only be used for creating one descriptor set!
        (uint32_t)pool_sizes.size(),                     // pool size count
        (const VkDescriptorPoolSize *)pool_sizes.data()  // ptr to descriptr pool sizes
    };

    VkResult result = vkCreateDescriptorPool(allocator->current_device(), &descriptor_pool_info, allocator->callbacks(), descriptor_pool);
    if (result != VK_SUCCESS) {
        debug(user_context) << "Vulkan: Failed to create descriptor pool! vkCreateDescriptorPool returned " << vk_get_error_name(result) << "\n";
        return result;
    }
    return VK_SUCCESS;
}

VkResult vk_destroy_descriptor_pool(void *user_context,
                                    VulkanMemoryAllocator *allocator,
                                    VkDescriptorPool descriptor_pool) {
#ifdef DEBUG_RUNTIME
    debug(user_context)
        << " vk_destroy_descriptor_pool (user_context: " << user_context << ", "
        << "allocator: " << (void *)allocator << ", "
        << "device: " << (void *)allocator->current_device() << ", "
        << "descriptor_pool: " << (void *)descriptor_pool << ")\n";
#endif
    vkDestroyDescriptorPool(allocator->current_device(), descriptor_pool, allocator->callbacks());
    return VK_SUCCESS;
}

// --

VkResult vk_create_descriptor_set_layout(void *user_context,
                                         VulkanMemoryAllocator *allocator,
                                         uint32_t uniform_buffer_count,
                                         uint32_t storage_buffer_count,
                                         VkDescriptorSetLayout *layout) {

#ifdef DEBUG_RUNTIME
    debug(user_context)
        << " vk_create_descriptor_set_layout (user_context: " << user_context << ", "
        << "allocator: " << (void *)allocator << ", "
        << "device: " << (void *)allocator->current_device() << ", "
        << "uniform_buffer_count: " << uniform_buffer_count << ", "
        << "storage_buffer_count: " << storage_buffer_count << ", "
        << "layout: " << (void *)layout << ")\n";
#endif

    BlockStorage::Config layout_config;
    layout_config.entry_size = sizeof(VkDescriptorSetLayoutBinding);
    layout_config.minimum_capacity = uniform_buffer_count + storage_buffer_count;
    BlockStorage layout_bindings(user_context, layout_config);

    // add all uniform buffers first
    for (uint32_t n = 0; n < uniform_buffer_count; ++n) {
        VkDescriptorSetLayoutBinding uniform_buffer_layout = {
            (uint32_t)layout_bindings.size(),   // binding index
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,  // descriptor type
            1,                                  // descriptor count
            VK_SHADER_STAGE_COMPUTE_BIT,        // stage flags
            nullptr                             // immutable samplers
        };

#ifdef DEBUG_RUNTIME
        debug(user_context)
            << "  [" << (uint32_t)layout_bindings.size() << "] : UNIFORM_BUFFER\n";
#endif

        layout_bindings.append(user_context, &uniform_buffer_layout);
    }

    // Add all other storage buffers
    for (uint32_t n = 0; n < storage_buffer_count; ++n) {

        // halide buffers will be passed as STORAGE_BUFFERS
        VkDescriptorSetLayoutBinding storage_buffer_layout = {
            (uint32_t)layout_bindings.size(),   // binding index
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,  // descriptor type
            1,                                  // descriptor count
            VK_SHADER_STAGE_COMPUTE_BIT,        // stage flags
            nullptr                             // immutable samplers
        };
#ifdef DEBUG_RUNTIME
        debug(user_context)
            << "  [" << (uint32_t)layout_bindings.size() << "] : STORAGE_BUFFER\n";
#endif

        layout_bindings.append(user_context, &storage_buffer_layout);
    }

    // Create the LayoutInfo struct
    VkDescriptorSetLayoutCreateInfo layout_info = {
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,    // structure type
        nullptr,                                                // pointer to a struct extending this info
        0,                                                      // flags
        (uint32_t)layout_bindings.size(),                       // binding count
        (VkDescriptorSetLayoutBinding *)layout_bindings.data()  // pointer to layout bindings array
    };

    // Create the descriptor set layout
    VkResult result = vkCreateDescriptorSetLayout(allocator->current_device(), &layout_info, allocator->callbacks(), layout);
    if (result != VK_SUCCESS) {
        error(user_context) << "vkCreateDescriptorSetLayout returned " << vk_get_error_name(result) << "\n";
        return result;
    }

    return VK_SUCCESS;
}

VkResult vk_destroy_descriptor_set_layout(void *user_context,
                                          VulkanMemoryAllocator *allocator,
                                          VkDescriptorSetLayout descriptor_set_layout) {

    vkDestroyDescriptorSetLayout(allocator->current_device(), descriptor_set_layout, allocator->callbacks());
    return VK_SUCCESS;
}

// --

VkResult vk_create_descriptor_set(void *user_context,
                                  VulkanMemoryAllocator *allocator,
                                  VkDescriptorSetLayout descriptor_set_layout,
                                  VkDescriptorPool descriptor_pool,
                                  VkDescriptorSet *descriptor_set) {
#ifdef DEBUG_RUNTIME
    debug(user_context)
        << " vk_create_descriptor_set (user_context: " << user_context << ", "
        << "allocator: " << (void *)allocator << ", "
        << "device: " << (void *)allocator->current_device() << ", "
        << "descriptor_set_layout: " << (void *)descriptor_set_layout << ", "
        << "descriptor_pool: " << (void *)descriptor_pool << ")\n";
#endif

    VkDescriptorSetAllocateInfo descriptor_set_info =
        {
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,  // struct type
            nullptr,                                         // pointer to struct extending this
            descriptor_pool,                                 // pool from which to allocate sets
            1,                                               // number of descriptor sets
            &descriptor_set_layout                           // pointer to array of descriptor set layouts
        };

    VkResult result = vkAllocateDescriptorSets(allocator->current_device(), &descriptor_set_info, descriptor_set);
    if (result != VK_SUCCESS) {
        debug(user_context) << "vkAllocateDescriptorSets returned " << vk_get_error_name(result) << "\n";
        return result;
    }

    return VK_SUCCESS;
}

VkResult vk_update_descriptor_set(void *user_context,
                                  VulkanMemoryAllocator *allocator,
                                  VkBuffer *scalar_args_buffer,
                                  size_t uniform_buffer_count,
                                  size_t storage_buffer_count,
                                  size_t arg_sizes[],
                                  void *args[],
                                  int8_t arg_is_buffer[],
                                  VkDescriptorSet descriptor_set) {
#ifdef DEBUG_RUNTIME
    debug(user_context)
        << " vk_update_descriptor_set (user_context: " << user_context << ", "
        << "allocator: " << (void *)allocator << ", "
        << "device: " << (void *)allocator->current_device() << ", "
        << "scalar_args_buffer: " << (void *)scalar_args_buffer << ", "
        << "uniform_buffer_count: " << (uint32_t)uniform_buffer_count << ", "
        << "storage_buffer_count: " << (uint32_t)storage_buffer_count << ", "
        << "descriptor_set: " << (void *)descriptor_set << ")\n";
#endif

    BlockStorage::Config dbi_config;
    dbi_config.minimum_capacity = storage_buffer_count + uniform_buffer_count;
    dbi_config.entry_size = sizeof(VkDescriptorBufferInfo);
    BlockStorage descriptor_buffer_info(user_context, dbi_config);

    BlockStorage::Config wds_config;
    wds_config.minimum_capacity = storage_buffer_count + uniform_buffer_count;
    wds_config.entry_size = sizeof(VkWriteDescriptorSet);
    BlockStorage write_descriptor_set(user_context, wds_config);

    // First binding will be the scalar args buffer (if needed) passed as a UNIFORM BUFFER
    VkDescriptorBufferInfo *scalar_args_entry = nullptr;
    if (scalar_args_buffer != nullptr) {
        VkDescriptorBufferInfo scalar_args_descriptor_buffer_info = {
            *scalar_args_buffer,  // the buffer
            0,                    // offset
            VK_WHOLE_SIZE         // range
        };
        descriptor_buffer_info.append(user_context, &scalar_args_descriptor_buffer_info);
        scalar_args_entry = (VkDescriptorBufferInfo *)descriptor_buffer_info.back();

        VkWriteDescriptorSet uniform_buffer_write_descriptor_set = {
            VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,  // struct type
            nullptr,                                 // pointer to struct extending this
            descriptor_set,                          // descriptor set to update
            0,                                       // binding slot
            0,                                       // array elem
            1,                                       // num to update
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,       // descriptor type
            nullptr,                                 // for images
            scalar_args_entry,                       // info for buffer
            nullptr                                  // for texel buffers
        };
        write_descriptor_set.append(user_context, &uniform_buffer_write_descriptor_set);
    }

    // Add all the other device buffers as STORAGE BUFFERs
    for (size_t i = 0; arg_sizes[i] > 0; i++) {
        if (arg_is_buffer[i]) {

            // get the allocated region for the buffer
            MemoryRegion *device_region = reinterpret_cast<MemoryRegion *>(((halide_buffer_t *)args[i])->device);

            // retrieve the buffer from the region
            VkBuffer *device_buffer = reinterpret_cast<VkBuffer *>(device_region->handle);
            if (device_buffer == nullptr) {
                error(user_context) << "Vulkan: Failed to retrieve buffer for device memory!\n";
                return VK_ERROR_INITIALIZATION_FAILED;
            }

            VkDescriptorBufferInfo device_buffer_info = {
                *device_buffer,  // the buffer
                0,               // offset
                VK_WHOLE_SIZE    // range
            };
            descriptor_buffer_info.append(user_context, &device_buffer_info);
            VkDescriptorBufferInfo *device_buffer_entry = (VkDescriptorBufferInfo *)descriptor_buffer_info.back();

            VkWriteDescriptorSet storage_buffer_write_descriptor_set = {
                VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,  // struct type
                nullptr,                                 // pointer to struct extending this
                descriptor_set,                          // descriptor set to update
                (uint32_t)write_descriptor_set.size(),   // binding slot
                0,                                       // array elem
                1,                                       // num to update
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,       // descriptor type
                nullptr,                                 // for images
                device_buffer_entry,                     // info for buffer
                nullptr                                  // for texel buffers
            };
            write_descriptor_set.append(user_context, &storage_buffer_write_descriptor_set);
        }
    }

    // issue the update call to populate the descriptor set
    vkUpdateDescriptorSets(allocator->current_device(), (uint32_t)write_descriptor_set.size(), (const VkWriteDescriptorSet *)write_descriptor_set.data(), 0, nullptr);
    return VK_SUCCESS;
}

// --

size_t vk_estimate_scalar_uniform_buffer_size(void *user_context,
                                              size_t arg_sizes[],
                                              void *args[],
                                              int8_t arg_is_buffer[]) {
    int i = 0;
    int scalar_uniform_buffer_size = 0;
    while (arg_sizes[i] > 0) {
        if (!arg_is_buffer[i]) {
            scalar_uniform_buffer_size += arg_sizes[i];
        }
        i++;
    }
    return scalar_uniform_buffer_size;
}

MemoryRegion *vk_create_scalar_uniform_buffer(void *user_context,
                                              VulkanMemoryAllocator *allocator,
                                              size_t scalar_buffer_size) {

#ifdef DEBUG_RUNTIME
    debug(user_context)
        << " vk_create_scalar_uniform_buffer (user_context: " << user_context << ", "
        << "allocator: " << (void *)allocator << ", "
        << "device: " << (void *)allocator->current_device() << ", "
        << "scalar_buffer_size: " << (uint32_t)scalar_buffer_size << ")\n";
#endif

    MemoryRequest request = {0};
    request.size = scalar_buffer_size;
    request.properties.usage = MemoryUsage::UniformStorage;
    request.properties.caching = MemoryCaching::UncachedCoherent;
    request.properties.visibility = MemoryVisibility::HostToDevice;

    // allocate a new region
    MemoryRegion *region = allocator->reserve(user_context, request);
    if ((region == nullptr) || (region->handle == nullptr)) {
        error(user_context) << "Vulkan: Failed to allocate device memory!\n";
        return nullptr;
    }

    // return the allocated region for the uniform buffer
    return region;
}

VkResult vk_update_scalar_uniform_buffer(void *user_context,
                                         VulkanMemoryAllocator *allocator,
                                         MemoryRegion *region,
                                         size_t arg_sizes[],
                                         void *args[],
                                         int8_t arg_is_buffer[]) {

#ifdef DEBUG_RUNTIME
    debug(user_context)
        << " vk_update_scalar_uniform_buffer (user_context: " << user_context << ", "
        << "region: " << (void *)region << ")\n";
#endif

    if ((region == nullptr) || (region->handle == nullptr)) {
        error(user_context) << "Vulkan: Invalid memory region used for updating scalar uniform buffer!\n";
        return VK_INCOMPLETE;
    }

    // map the region to a host ptr
    uint8_t *host_ptr = (uint8_t *)allocator->map(user_context, region);
    if (host_ptr == nullptr) {
        error(user_context) << "Vulkan: Failed to map host pointer to device memory!\n";
        return VK_INCOMPLETE;
    }

    // copy to the (host-visible/coherent) scalar uniform buffer
    size_t arg_offset = 0;
    for (size_t i = 0; arg_sizes[i] > 0; i++) {
        if (!arg_is_buffer[i]) {
            memcpy(host_ptr + arg_offset, args[i], arg_sizes[i]);
            arg_offset += arg_sizes[i];
        }
    }

    // unmap the pointer to the buffer for the region
    allocator->unmap(user_context, region);
    return VK_SUCCESS;
}

void vk_destroy_scalar_uniform_buffer(void *user_context, VulkanMemoryAllocator *allocator,
                                      MemoryRegion *scalar_args_region) {

#ifdef DEBUG_RUNTIME
    debug(user_context)
        << " vk_destroy_scalar_uniform_buffer (user_context: " << user_context << ", "
        << "allocator: " << (void *)allocator << ", "
        << "device: " << (void *)allocator->current_device() << ", "
        << "scalar_args_region: " << (void *)scalar_args_region << ")\n";
#endif

    if (!scalar_args_region) {
        return;
    }
    allocator->reclaim(user_context, scalar_args_region);
}

// --

VkResult vk_create_pipeline_layout(void *user_context,
                                   VulkanMemoryAllocator *allocator,
                                   uint32_t descriptor_set_count,
                                   VkDescriptorSetLayout *descriptor_set_layouts,
                                   VkPipelineLayout *pipeline_layout) {

#ifdef DEBUG_RUNTIME
    debug(user_context)
        << " vk_create_pipeline_layout (user_context: " << user_context << ", "
        << "allocator: " << (void *)allocator << ", "
        << "device: " << (void *)allocator->current_device() << ", "
        << "descriptor_set_count: " << descriptor_set_count << ", "
        << "descriptor_set_layouts: " << (void *)descriptor_set_layouts << ", "
        << "pipeline_layout: " << (void *)pipeline_layout << ")\n";
#endif

    VkPipelineLayoutCreateInfo pipeline_layout_info = {
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,  // structure type
        nullptr,                                        // pointer to a structure extending this
        0,                                              // flags
        descriptor_set_count,                           // number of descriptor sets
        descriptor_set_layouts,                         // pointer to the descriptor sets
        0,                                              // number of push constant ranges
        nullptr                                         // pointer to push constant range structs
    };

    VkResult result = vkCreatePipelineLayout(allocator->current_device(), &pipeline_layout_info, allocator->callbacks(), pipeline_layout);
    if (result != VK_SUCCESS) {
        debug(user_context) << "vkCreatePipelineLayout returned " << vk_get_error_name(result) << "\n";
        return result;
    }
    return VK_SUCCESS;
}

VkResult vk_destroy_pipeline_layout(void *user_context,
                                    VulkanMemoryAllocator *allocator,
                                    VkPipelineLayout pipeline_layout) {

#ifdef DEBUG_RUNTIME
    debug(user_context)
        << " vk_destroy_pipeline_layout (user_context: " << user_context << ", "
        << "allocator: " << (void *)allocator << ", "
        << "device: " << (void *)allocator->current_device() << ", "
        << "pipeline_layout: " << (void *)pipeline_layout << ")\n";
#endif

    vkDestroyPipelineLayout(allocator->current_device(), pipeline_layout, allocator->callbacks());
    return VK_SUCCESS;
}

// --

VkResult vk_create_compute_pipeline(void *user_context,
                                    VulkanMemoryAllocator *allocator,
                                    const char *pipeline_name,
                                    VkShaderModule shader_module,
                                    VkPipelineLayout pipeline_layout,
                                    VkPipeline *compute_pipeline) {

#ifdef DEBUG_RUNTIME
    debug(user_context)
        << " vk_create_compute_pipeline (user_context: " << user_context << ", "
        << "allocator: " << (void *)allocator << ", "
        << "device: " << (void *)allocator->current_device() << ", "
        << "shader_module: " << (void *)shader_module << ", "
        << "pipeline_layout: " << (void *)pipeline_layout << ")\n";
#endif

    VkComputePipelineCreateInfo compute_pipeline_info =
        {
            VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,  // structure type
            nullptr,                                         // pointer to a structure extending this
            0,                                               // flags
            // VkPipelineShaderStageCreatInfo
            {
                VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,  // structure type
                nullptr,                                              // pointer to a structure extending this
                0,                                                    // flags
                VK_SHADER_STAGE_COMPUTE_BIT,                          // compute stage shader
                shader_module,                                        // shader module
                pipeline_name,                                        // entry point name
                nullptr                                               // pointer to VkSpecializationInfo struct
            },
            pipeline_layout,  // pipeline layout
            0,                // base pipeline handle for derived pipeline
            0                 // base pipeline index for derived pipeline
        };

    VkResult result = vkCreateComputePipelines(allocator->current_device(), 0, 1, &compute_pipeline_info, allocator->callbacks(), compute_pipeline);
    if (result != VK_SUCCESS) {
        error(user_context) << "Vulkan: Failed to create compute pipeline! vkCreateComputePipelines returned " << vk_get_error_name(result) << "\n";
        return result;
    }

    return VK_SUCCESS;
}

VkResult vk_destroy_compute_pipeline(void *user_context,
                                     VulkanMemoryAllocator *allocator,
                                     VkPipeline compute_pipeline) {
#ifdef DEBUG_RUNTIME
    debug(user_context)
        << " vk_destroy_compute_pipeline (user_context: " << user_context << ", "
        << "allocator: " << (void *)allocator << ", "
        << "device: " << (void *)allocator->current_device() << ", "
        << "compute_pipeline: " << (void *)compute_pipeline << ")\n";
#endif
    vkDestroyPipeline(allocator->current_device(), compute_pipeline, allocator->callbacks());
    return VK_SUCCESS;
}

// --------------------------------------------------------------------------

VulkanEntryPointData *vk_decode_entry_point_data(void *user_context, VulkanMemoryAllocator *allocator, const uint32_t *module_ptr, uint32_t module_size) {
#ifdef DEBUG_RUNTIME
    debug(user_context)
        << " vk_decode_entry_point_data (user_context: " << user_context << ", "
        << "allocator: " << (void *)allocator << ", "
        << "module_ptr: " << (void *)module_ptr << ", "
        << "module_size: " << module_size << ")\n";

    uint64_t t_before = halide_current_time_ns(user_context);
#endif
    halide_debug_assert(user_context, module_ptr != nullptr);
    halide_debug_assert(user_context, module_size >= (2 * sizeof(uint32_t)));

    // Decode the sidecar for the module that lists the descriptor sets
    // corresponding to each entry point contained in the module
    //
    // [0] Header word count (total length of header)
    // [1] Number of descriptor sets
    // ... For each descriptor set ...
    // ... [0] Number of uniform buffers for this descriptor set
    // ... [1] Number of storage buffers for this descriptor set
    // ... [2] Length of entry point name (padded to nearest word size)
    // ... [X] Entry point string data
    //
    // NOTE: See CodeGen_Vulkan_Dev::SPIRV_Emitter::encode_header() for the encoding
    //
    //
    uint32_t module_entries = module_size / sizeof(uint32_t);
    uint32_t idx = 1;  // skip past the header_word_count
    uint32_t entry_point_count = module_ptr[idx++];
    if (entry_point_count < 1) {
        return nullptr;  // no descriptors
    }

    // allocate an array of entry point data
    VkSystemAllocationScope alloc_scope = VkSystemAllocationScope::VK_SYSTEM_ALLOCATION_SCOPE_OBJECT;
    size_t entry_point_data_size = entry_point_count * sizeof(VulkanEntryPointData);
    VulkanEntryPointData *entry_point_data = (VulkanEntryPointData *)vk_host_malloc(user_context, entry_point_data_size, 0, alloc_scope, allocator->callbacks());
    if (entry_point_data == nullptr) {
        error(user_context) << "Vulkan: Failed to allocate entry_point_data! Out of memory!\n";
        return nullptr;
    }
    memset(entry_point_data, 0, entry_point_data_size);

    // decode and fill in each entry point
    for (uint32_t n = 0; (n < entry_point_count) && (idx < module_entries); n++) {
        halide_debug_assert(user_context, (idx + 4) < module_entries);
        uint32_t uniform_buffer_count = module_ptr[idx++];
        uint32_t storage_buffer_count = module_ptr[idx++];
        uint32_t padded_string_length = module_ptr[idx++];
        const char *entry_point_name = (const char *)(module_ptr + idx);

        debug(user_context) << "    [" << n << "] "
                            << "uniform_buffer_count=" << uniform_buffer_count << " "
                            << "storage_buffer_count=" << storage_buffer_count << " "
                            << "entry_point_name_length=" << padded_string_length << " "
                            << "entry_point_name: " << (const char *)entry_point_name << "\n";

        entry_point_data[n].entry_point_name = entry_point_name;  // NOTE: module owns string data
        entry_point_data[n].uniform_buffer_count = uniform_buffer_count;
        entry_point_data[n].storage_buffer_count = storage_buffer_count;
        idx += (padded_string_length / sizeof(uint32_t));  // skip past string data
    }

#ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
#endif

    return entry_point_data;
}

VulkanCompilationCacheEntry *vk_compile_shader_module(void *user_context, VulkanMemoryAllocator *allocator,
                                                      const char *ptr, int size) {
#ifdef DEBUG_RUNTIME
    debug(user_context)
        << " vk_compile_shader_module (user_context: " << user_context << ", "
        << "allocator: " << (void *)allocator << ", "
        << "device: " << (void *)allocator->current_device() << ", "
        << "module: " << (void *)ptr << ", "
        << "size: " << size << ")\n";

    uint64_t t_before = halide_current_time_ns(user_context);
#endif

    const uint32_t *module_ptr = (const uint32_t *)ptr;
    const uint32_t module_size = (const uint32_t)size;

    halide_debug_assert(user_context, module_ptr != nullptr);
    halide_debug_assert(user_context, module_size >= (2 * sizeof(uint32_t)));

    uint32_t header_word_count = module_ptr[0];
    uint32_t entry_point_count = module_ptr[1];
    uint32_t header_size = header_word_count * sizeof(uint32_t);

    // skip past the preamble header to the start of the SPIR-V binary
    const uint32_t *binary_ptr = (module_ptr + header_word_count);
    size_t binary_size = (size - header_size);

#ifdef DEBUG_RUNTIME
    debug(user_context) << "Vulkan: Decoding module ("
                        << "module_ptr: " << (void *)module_ptr << ", "
                        << "header_word_count: " << header_word_count << ", "
                        << "header_size: " << header_size << ", "
                        << "binar_ptr: " << (void *)binary_ptr << ", "
                        << "binary_size: " << (uint32_t)binary_size << ")\n";
#endif

    VkShaderModuleCreateInfo shader_info = {
        VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        nullptr,                      // pointer to structure extending this
        0,                            // flags (curently unused)
        (size_t)binary_size,          // code size in bytes
        (const uint32_t *)binary_ptr  // source
    };

    VkSystemAllocationScope alloc_scope = VkSystemAllocationScope::VK_SYSTEM_ALLOCATION_SCOPE_OBJECT;
    VulkanCompilationCacheEntry *cache_entry = (VulkanCompilationCacheEntry *)vk_host_malloc(user_context, sizeof(VulkanCompilationCacheEntry), 0, alloc_scope, allocator->callbacks());
    if (cache_entry == nullptr) {
        error(user_context) << "Vulkan: Failed to allocate compilation cache entry! Out of memory!\n";
        return nullptr;
    }
    memset(cache_entry, 0, sizeof(VulkanCompilationCacheEntry));

    // decode the entry point data and save it in the cache entry
    cache_entry->entry_point_data = vk_decode_entry_point_data(user_context, allocator, module_ptr, module_size);
    if (cache_entry->entry_point_data != nullptr) {
        cache_entry->entry_point_count = entry_point_count;
    }

    VkResult result = vkCreateShaderModule(allocator->current_device(), &shader_info, allocator->callbacks(), &cache_entry->shader_module);
    if ((result != VK_SUCCESS)) {
        error(user_context) << "Vulkan: vkCreateShaderModule Failed! Error returned: " << vk_get_error_name(result) << "\n";
        vk_host_free(user_context, cache_entry->entry_point_data, allocator->callbacks());
        vk_host_free(user_context, cache_entry, allocator->callbacks());
        return nullptr;
    }

    // allocate an array for storing the descriptor set layouts
    if (cache_entry->entry_point_count) {
        cache_entry->descriptor_set_layouts = (VkDescriptorSetLayout *)vk_host_malloc(user_context, cache_entry->entry_point_count * sizeof(VkDescriptorSetLayout), 0, alloc_scope, allocator->callbacks());
        if (cache_entry->descriptor_set_layouts == nullptr) {
            error(user_context) << "Vulkan: Failed to allocate descriptor set layouts for cache entry! Out of memory!\n";
            return nullptr;
        }
        memset(cache_entry->descriptor_set_layouts, 0, cache_entry->entry_point_count * sizeof(VkDescriptorSetLayout));
    }

#ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
#endif

    return cache_entry;
}

int vk_destroy_shader_modules(void *user_context, VulkanMemoryAllocator *allocator) {

#ifdef DEBUG_RUNTIME
    debug(user_context)
        << " vk_destroy_shader_modules (user_context: " << user_context << ", "
        << "allocator: " << (void *)allocator << ", "
        << "device: " << (void *)allocator->current_device() << ")\n";

    uint64_t t_before = halide_current_time_ns(user_context);
#endif

    // Functor to match compilation cache destruction call with scoped params
    struct DestroyShaderModule {
        void *user_context = nullptr;
        VulkanMemoryAllocator *allocator = nullptr;

        DestroyShaderModule(void *ctx, VulkanMemoryAllocator *allocator)
            : user_context(ctx), allocator(allocator) {
        }

        void operator()(VulkanCompilationCacheEntry *cache_entry) {
            if (cache_entry != nullptr) {
                if (cache_entry->shader_module) {
                    debug(user_context) << "    destroying shader module " << (void*)cache_entry->shader_module << "\n";
                    vkDestroyShaderModule(allocator->current_device(), cache_entry->shader_module, allocator->callbacks());
                    cache_entry->shader_module = {0};
                }
                if (cache_entry->entry_point_data) {
                    for (uint32_t n = 0; n < cache_entry->entry_point_count; n++) {
                        if (cache_entry->entry_point_data[n].args_region) {
                            vk_destroy_scalar_uniform_buffer(user_context, allocator, cache_entry->entry_point_data[n].args_region);
                            cache_entry->entry_point_data[n].args_region = nullptr;
                        }
                        if (cache_entry->entry_point_data[n].descriptor_pool) {
                            vk_destroy_descriptor_pool(user_context, allocator, cache_entry->entry_point_data[n].descriptor_pool);
                            cache_entry->entry_point_data[n].descriptor_pool = {0};
                        }
                        if (cache_entry->entry_point_data[n].compute_pipeline) {
                            vk_destroy_compute_pipeline(user_context, allocator, cache_entry->entry_point_data[n].compute_pipeline);
                            cache_entry->entry_point_data[n].compute_pipeline = {0};
                        }
                    }

                    vk_host_free(user_context, cache_entry->entry_point_data, allocator->callbacks());
                    cache_entry->entry_point_data = nullptr;
                    cache_entry->entry_point_count = 0;
                }
                if (cache_entry->descriptor_set_layouts) {
                    for (uint32_t n = 0; n < cache_entry->entry_point_count; n++) {
                        debug(user_context) << "    destroying descriptor set layout [" << n << "] " << cache_entry->entry_point_data[n].entry_point_name << "\n";
                        vk_destroy_descriptor_set_layout(user_context, allocator, cache_entry->descriptor_set_layouts[n]);
                        cache_entry->descriptor_set_layouts[n] = {0};
                    }
                    vk_host_free(user_context, cache_entry->descriptor_set_layouts, allocator->callbacks());
                    cache_entry->descriptor_set_layouts = nullptr;
                }
                if (cache_entry->pipeline_layout) {
                    debug(user_context) << "    destroying pipeline layout " << (void*)cache_entry->pipeline_layout << "\n";
                    vk_destroy_pipeline_layout(user_context, allocator, cache_entry->pipeline_layout);
                    cache_entry->pipeline_layout = {0};
                }

                vk_host_free(user_context, cache_entry, allocator->callbacks());
            }
        }
    };

    DestroyShaderModule module_destructor(user_context, allocator);
    compilation_cache.delete_context(user_context, allocator->current_device(), module_destructor);

#ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
#endif
    return VK_SUCCESS;
}

// --------------------------------------------------------------------------

int vk_do_multidimensional_copy(void *user_context, VkCommandBuffer command_buffer,
                                const device_copy &c, uint64_t src_offset, uint64_t dst_offset, int d) {
    if (d == 0) {

        VkBufferCopy buffer_copy = {
            c.src_begin + src_offset,  // srcOffset
            dst_offset,                // dstOffset
            c.chunk_size               // size
        };

        VkBuffer *src_buffer = reinterpret_cast<VkBuffer *>(c.src);
        VkBuffer *dst_buffer = reinterpret_cast<VkBuffer *>(c.dst);
        if (!src_buffer || !dst_buffer) {
            error(user_context) << "Vulkan: Failed to retrieve buffer for device memory!\n";
            return -1;
        }

        vkCmdCopyBuffer(command_buffer, *src_buffer, *dst_buffer, 1, &buffer_copy);

    } else {
        // TODO: deal with negative strides. Currently the code in
        // device_buffer_utils.h does not do so either.
        uint64_t src_off = 0, dst_off = 0;
        for (uint64_t i = 0; i < c.extent[d - 1]; i++) {
            int err = vk_do_multidimensional_copy(user_context, command_buffer, c, src_offset + src_off, dst_offset + dst_off, d - 1);
            dst_off += c.dst_stride_bytes[d - 1];
            src_off += c.src_stride_bytes[d - 1];
            if (err) {
                return err;
            }
        }
    }
    return 0;
}

// --------------------------------------------------------------------------

}  // namespace
}  // namespace Vulkan
}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

#endif  // HALIDE_RUNTIME_VULKAN_RESOURCES_H
