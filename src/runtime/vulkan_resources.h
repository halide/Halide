#ifndef HALIDE_RUNTIME_VULKAN_RESOURCES_H
#define HALIDE_RUNTIME_VULKAN_RESOURCES_H

#include "vulkan_internal.h"
#include "vulkan_memory.h"

// --------------------------------------------------------------------------

namespace Halide {
namespace Runtime {
namespace Internal {
namespace Vulkan {

// Defines the specialization constants used for dynamically overiding the dispatch size
struct VulkanWorkgroupSizeBinding {
    uint32_t constant_id[3] = {0};  // zero if unused
};

// Data used to override specialization constants for dynamic dispatching
struct VulkanDispatchData {
    uint32_t global_size[3] = {0};  // aka blocks
    uint32_t local_size[3] = {0};   // aka threads
    uint32_t shared_mem_bytes = 0;
    VulkanWorkgroupSizeBinding local_size_binding = {};
};

// Specialization constant binding information
struct VulkanSpecializationConstant {
    uint32_t constant_id = 0;
    uint32_t type_size = 0;
    const char *constant_name = nullptr;
};

// Shared memory allocation variable information
struct VulkanSharedMemoryAllocation {
    uint32_t constant_id = 0;  // specialization constant to override allocation array size (or zero if unused)
    uint32_t type_size = 0;
    uint32_t array_size = 0;
    const char *variable_name = nullptr;
};

// Entry point metadata for shader modules
struct VulkanShaderBinding {
    char *entry_point_name = nullptr;
    VulkanDispatchData dispatch_data = {};
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
    VkPipeline compute_pipeline = VK_NULL_HANDLE;
    uint32_t uniform_buffer_count = 0;
    uint32_t storage_buffer_count = 0;
    uint32_t specialization_constants_count = 0;
    uint32_t shared_memory_allocations_count = 0;
    VulkanSpecializationConstant *specialization_constants = nullptr;
    VulkanSharedMemoryAllocation *shared_memory_allocations = nullptr;
    uint32_t bindings_count = 0;
    MemoryRegion *args_region = nullptr;
};

// Compiled shader module and associated bindings
struct VulkanCompiledShaderModule {
    VkShaderModule shader_module = VK_NULL_HANDLE;
    VkDescriptorSetLayout *descriptor_set_layouts = nullptr;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    uint32_t shader_count = 0;
    VulkanShaderBinding *shader_bindings = nullptr;
};

// Compilation cache for compiled shader modules
struct VulkanCompilationCacheEntry {
    VulkanMemoryAllocator *allocator = nullptr;
    VulkanCompiledShaderModule **compiled_modules = nullptr;
    uint32_t module_count = 0;
};

WEAK Halide::Internal::GPUCompilationCache<VkDevice, VulkanCompilationCacheEntry *> compilation_cache;

// --------------------------------------------------------------------------

namespace {  // internalize

// --------------------------------------------------------------------------

int vk_create_command_pool(void *user_context, VulkanMemoryAllocator *allocator, uint32_t queue_index, VkCommandPool *command_pool) {
#ifdef DEBUG_RUNTIME
    debug(user_context)
        << " vk_create_command_pool (user_context: " << user_context << ", "
        << "allocator: " << (void *)allocator << ", "
        << "queue_index: " << queue_index << ")";
#endif

    if (allocator == nullptr) {
        error(user_context) << "Vulkan: Failed to create command pool ... invalid allocator pointer!\n";
        return halide_error_code_generic_error;
    }

    VkCommandPoolCreateInfo command_pool_info =
        {
            VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,  // struct type
            nullptr,                                     // pointer to struct extending this
            VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,        // flags. Assume transient short-lived single-use command buffers
            queue_index                                  // queue family index corresponding to the compute command queue
        };

    VkResult result = vkCreateCommandPool(allocator->current_device(), &command_pool_info, allocator->callbacks(), command_pool);
    if (result != VK_SUCCESS) {
        vk_report_error(user_context, result, "vkCreateCommandPool");
        return halide_error_code_generic_error;
    }
    return halide_error_code_success;
}

int vk_destroy_command_pool(void *user_context, VulkanMemoryAllocator *allocator, VkCommandPool command_pool) {
#ifdef DEBUG_RUNTIME
    debug(user_context)
        << " vk_destroy_command_pool (user_context: " << user_context << ", "
        << "allocator: " << (void *)allocator << ", "
        << "command_pool: " << (void *)command_pool << ")\n";
#endif
    if (allocator == nullptr) {
        error(user_context) << "Vulkan: Failed to destroy command pool ... invalid allocator pointer!";
        return halide_error_code_generic_error;
    }
    vkResetCommandPool(allocator->current_device(), command_pool, VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT);
    vkDestroyCommandPool(allocator->current_device(), command_pool, allocator->callbacks());
    return halide_error_code_success;
}

// --

int vk_create_command_buffer(void *user_context, VulkanMemoryAllocator *allocator, VkCommandPool command_pool, VkCommandBuffer *command_buffer) {
#ifdef DEBUG_RUNTIME
    debug(user_context)
        << " vk_create_command_buffer (user_context: " << user_context << ", "
        << "allocator: " << (void *)allocator << ", "
        << "command_pool: " << (void *)command_pool << ")\n";
#endif
    if (allocator == nullptr) {
        error(user_context) << "Vulkan: Failed to create command buffer ... invalid allocator pointer!";
        return halide_error_code_generic_error;
    }

    VkCommandBufferAllocateInfo command_buffer_info =
        {
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,  // struct type
            nullptr,                                         // pointer to struct extending this
            command_pool,                                    // command pool for allocation
            VK_COMMAND_BUFFER_LEVEL_PRIMARY,                 // command buffer level
            1                                                // number to allocate
        };

    VkResult result = vkAllocateCommandBuffers(allocator->current_device(), &command_buffer_info, command_buffer);
    if (result != VK_SUCCESS) {
        vk_report_error(user_context, result, "vkAllocateCommandBuffers");
        return halide_error_code_generic_error;
    }
    return halide_error_code_success;
}

int vk_destroy_command_buffer(void *user_context, VulkanMemoryAllocator *allocator, VkCommandPool command_pool, VkCommandBuffer command_buffer) {
#ifdef DEBUG_RUNTIME
    debug(user_context)
        << " vk_destroy_command_buffer (user_context: " << user_context << ", "
        << "allocator: " << (void *)allocator << ", "
        << "command_pool: " << (void *)command_pool << ", "
        << "command_buffer: " << (void *)command_buffer << ")\n";
#endif
    if (allocator == nullptr) {
        error(user_context) << "Vulkan: Failed to destroy command buffer ... invalid allocator pointer!";
        return halide_error_code_generic_error;
    }

    vkFreeCommandBuffers(allocator->current_device(), command_pool, 1, &command_buffer);
    return halide_error_code_success;
}

// Struct for handling destruction of a transient command buffer ... gets destroyed when object goes out of scope
struct ScopedVulkanCommandBufferAndPool {
    void *user_context = nullptr;
    VulkanMemoryAllocator *allocator = nullptr;
    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    int error_code = halide_error_code_success;

    ScopedVulkanCommandBufferAndPool(void *uc, VulkanMemoryAllocator *vma, uint32_t queue_family_index)
        : user_context(uc), allocator(vma) {
        error_code = vk_create_command_pool(user_context, allocator, queue_family_index, &command_pool);
        if (error_code == halide_error_code_success) {
            error_code = vk_create_command_buffer(user_context, allocator, command_pool, &command_buffer);
        }
    }
    ~ScopedVulkanCommandBufferAndPool() {
        if ((allocator != nullptr) && (command_pool != VK_NULL_HANDLE)) {
            if (command_buffer != VK_NULL_HANDLE) {
                vk_destroy_command_buffer(user_context, allocator, command_pool, command_buffer);
            }
            vk_destroy_command_pool(user_context, allocator, command_pool);
        }
        user_context = nullptr;
        allocator = nullptr;
        command_pool = VK_NULL_HANDLE;
        command_buffer = VK_NULL_HANDLE;
    }
};

int vk_fill_command_buffer_with_dispatch_call(void *user_context,
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
        vk_report_error(user_context, result, "vkBeginCommandBuffer");
        return halide_error_code_generic_error;
    }

    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute_pipeline);
    vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout,
                            descriptor_set_index, 1, &descriptor_set, 0, nullptr);
    vkCmdDispatch(command_buffer, blocksX, blocksY, blocksZ);

    result = vkEndCommandBuffer(command_buffer);
    if (result != VK_SUCCESS) {
        vk_report_error(user_context, result, "vkEndCommandBuffer");
        return halide_error_code_generic_error;
    }

    return halide_error_code_success;
}

int vk_submit_command_buffer(void *user_context, VkQueue queue, VkCommandBuffer command_buffer) {
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

    VkResult result = vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE);
    if (result != VK_SUCCESS) {
        vk_report_error(user_context, result, "vkSubmitQueue");
        return halide_error_code_generic_error;
    }
    return halide_error_code_success;
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

int vk_create_descriptor_pool(void *user_context,
                              VulkanMemoryAllocator *allocator,
                              uint32_t uniform_buffer_count,
                              uint32_t storage_buffer_count,
                              VkDescriptorPool *descriptor_pool) {
#ifdef DEBUG_RUNTIME
    debug(user_context)
        << " vk_create_descriptor_pool (user_context: " << user_context << ", "
        << "allocator: " << (void *)allocator << ", "
        << "uniform_buffer_count: " << (uint32_t)uniform_buffer_count << ", "
        << "storage_buffer_count: " << (uint32_t)storage_buffer_count << ")\n";
#endif
    if (allocator == nullptr) {
        error(user_context) << "Vulkan: Failed to create descriptor pool ... invalid allocator pointer!";
        return halide_error_code_generic_error;
    }

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
        vk_report_error(user_context, result, "vkCreateDescriptorPool");
        return halide_error_code_generic_error;
    }
    return halide_error_code_success;
}

int vk_destroy_descriptor_pool(void *user_context,
                               VulkanMemoryAllocator *allocator,
                               VkDescriptorPool descriptor_pool) {
#ifdef DEBUG_RUNTIME
    debug(user_context)
        << " vk_destroy_descriptor_pool (user_context: " << user_context << ", "
        << "allocator: " << (void *)allocator << ", "
        << "descriptor_pool: " << (void *)descriptor_pool << ")\n";
#endif
    if (allocator == nullptr) {
        error(user_context) << "Vulkan: Failed to destroy descriptor pool ... invalid allocator pointer!";
        return halide_error_code_generic_error;
    }
    vkDestroyDescriptorPool(allocator->current_device(), descriptor_pool, allocator->callbacks());
    return halide_error_code_success;
}

// --

int vk_create_descriptor_set_layout(void *user_context,
                                    VulkanMemoryAllocator *allocator,
                                    uint32_t uniform_buffer_count,
                                    uint32_t storage_buffer_count,
                                    VkDescriptorSetLayout *layout) {

#ifdef DEBUG_RUNTIME
    debug(user_context)
        << " vk_create_descriptor_set_layout (user_context: " << user_context << ", "
        << "allocator: " << (void *)allocator << ", "
        << "uniform_buffer_count: " << uniform_buffer_count << ", "
        << "storage_buffer_count: " << storage_buffer_count << ", "
        << "layout: " << (void *)layout << ")\n";
#endif
    if (allocator == nullptr) {
        error(user_context) << "Vulkan: Failed to create descriptor set layout ... invalid allocator pointer!";
        return halide_error_code_generic_error;
    }

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
        vk_report_error(user_context, result, "vkCreateDescriptorSetLayout");
        return halide_error_code_generic_error;
    }

    return halide_error_code_success;
}

int vk_destroy_descriptor_set_layout(void *user_context,
                                     VulkanMemoryAllocator *allocator,
                                     VkDescriptorSetLayout descriptor_set_layout) {

#ifdef DEBUG_RUNTIME
    debug(user_context)
        << " vk_destroy_descriptor_set_layout (user_context: " << user_context << ", "
        << "allocator: " << (void *)allocator << ", "
        << "layout: " << (void *)descriptor_set_layout << ")\n";
#endif
    if (allocator == nullptr) {
        error(user_context) << "Vulkan: Failed to destroy descriptor set layout ... invalid allocator pointer!";
        return halide_error_code_generic_error;
    }
    vkDestroyDescriptorSetLayout(allocator->current_device(), descriptor_set_layout, allocator->callbacks());
    return halide_error_code_success;
}

// --

int vk_create_descriptor_set(void *user_context,
                             VulkanMemoryAllocator *allocator,
                             VkDescriptorSetLayout descriptor_set_layout,
                             VkDescriptorPool descriptor_pool,
                             VkDescriptorSet *descriptor_set) {
#ifdef DEBUG_RUNTIME
    debug(user_context)
        << " vk_create_descriptor_set (user_context: " << user_context << ", "
        << "allocator: " << (void *)allocator << ", "
        << "descriptor_set_layout: " << (void *)descriptor_set_layout << ", "
        << "descriptor_pool: " << (void *)descriptor_pool << ")\n";
#endif
    if (allocator == nullptr) {
        error(user_context) << "Vulkan: Failed to create descriptor set ... invalid allocator pointer!";
        return halide_error_code_generic_error;
    }

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
        vk_report_error(user_context, result, "vkAllocateDescriptorSets");
        return halide_error_code_generic_error;
    }

    return halide_error_code_success;
}

int vk_update_descriptor_set(void *user_context,
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
        << "scalar_args_buffer: " << (void *)scalar_args_buffer << ", "
        << "uniform_buffer_count: " << (uint32_t)uniform_buffer_count << ", "
        << "storage_buffer_count: " << (uint32_t)storage_buffer_count << ", "
        << "descriptor_set: " << (void *)descriptor_set << ")\n";
#endif
    if (allocator == nullptr) {
        error(user_context) << "Vulkan: Failed to create descriptor set ... invalid allocator pointer!";
        return halide_error_code_generic_error;
    }

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

#ifdef DEBUG_RUNTIME
        debug(user_context) << "  [" << (uint32_t)write_descriptor_set.size() << "] UNIFORM_BUFFER : "
                            << "buffer=" << (void *)scalar_args_buffer << " "
                            << "offset=" << (uint32_t)(0) << " "
                            << "size=VK_WHOLE_SIZE\n";
#endif
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
            MemoryRegion *owner = allocator->owner_of(user_context, device_region);

            // retrieve the buffer from the region
            VkBuffer *device_buffer = reinterpret_cast<VkBuffer *>(owner->handle);
            if (device_buffer == nullptr) {
                error(user_context) << "Vulkan: Failed to retrieve buffer for device memory!";
                return halide_error_code_internal_error;
            }

            VkDeviceSize range_offset = device_region->range.head_offset;
            VkDeviceSize range_size = device_region->size - device_region->range.head_offset - device_region->range.tail_offset;
            halide_abort_if_false(user_context, (device_region->size - device_region->range.head_offset - device_region->range.tail_offset) > 0);
            VkDescriptorBufferInfo device_buffer_info = {
                *device_buffer,  // the buffer
                range_offset,    // range offset
                range_size       // range size
            };
            descriptor_buffer_info.append(user_context, &device_buffer_info);
            VkDescriptorBufferInfo *device_buffer_entry = (VkDescriptorBufferInfo *)descriptor_buffer_info.back();

#ifdef DEBUG_RUNTIME
            debug(user_context) << "  [" << (uint32_t)write_descriptor_set.size() << "] STORAGE_BUFFER : "
                                << "region=" << (void *)device_region << " "
                                << "buffer=" << (void *)device_buffer << " "
                                << "offset=" << (uint32_t)(range_offset) << " "
                                << "size=" << (uint32_t)(range_size) << "\n";
#endif

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
    return halide_error_code_success;
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
        << "scalar_buffer_size: " << (uint32_t)scalar_buffer_size << ")\n";
#endif

    if (allocator == nullptr) {
        error(user_context) << "Vulkan: Failed to create scalar uniform buffer ... invalid allocator pointer!";
        return nullptr;
    }

    MemoryRequest request = {0};
    request.size = scalar_buffer_size;
    request.properties.usage = MemoryUsage::UniformStorage;
    request.properties.caching = MemoryCaching::UncachedCoherent;
    request.properties.visibility = MemoryVisibility::HostToDevice;

    // allocate a new region
    MemoryRegion *region = allocator->reserve(user_context, request);
    if ((region == nullptr) || (region->handle == nullptr)) {
        error(user_context) << "Vulkan: Failed to create scalar uniform buffer ... unable to allocate device memory!";
        return nullptr;
    }

    // return the allocated region for the uniform buffer
    return region;
}

int vk_update_scalar_uniform_buffer(void *user_context,
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

    if (allocator == nullptr) {
        error(user_context) << "Vulkan: Failed to update scalar uniform buffer ... invalid allocator pointer!";
        return halide_error_code_generic_error;
    }

    if ((region == nullptr) || (region->handle == nullptr)) {
        error(user_context) << "Vulkan: Failed to update scalar uniform buffer ... invalid memory region!";
        return halide_error_code_internal_error;
    }

    // map the region to a host ptr
    uint8_t *host_ptr = (uint8_t *)allocator->map(user_context, region);
    if (host_ptr == nullptr) {
        error(user_context) << "Vulkan: Failed to update scalar uniform buffer ... unable to map host pointer to device memory!";
        return halide_error_code_internal_error;
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
    return halide_error_code_success;
}

int vk_destroy_scalar_uniform_buffer(void *user_context, VulkanMemoryAllocator *allocator,
                                     MemoryRegion *scalar_args_region) {

#ifdef DEBUG_RUNTIME
    debug(user_context)
        << " vk_destroy_scalar_uniform_buffer (user_context: " << user_context << ", "
        << "allocator: " << (void *)allocator << ", "
        << "scalar_args_region: " << (void *)scalar_args_region << ")\n";
#endif
    if (allocator == nullptr) {
        error(user_context) << "Vulkan: Failed to destroy scalar uniform buffer ... invalid allocator pointer!";
        return halide_error_code_generic_error;
    }

    if (!scalar_args_region) {
        return halide_error_code_success;
    }

    int error_code = halide_error_code_success;
    if (halide_can_reuse_device_allocations(user_context)) {
        error_code = allocator->release(user_context, scalar_args_region);
    } else {
        error_code = allocator->reclaim(user_context, scalar_args_region);
    }
    return error_code;
}

// --

int vk_create_pipeline_layout(void *user_context,
                              VulkanMemoryAllocator *allocator,
                              uint32_t descriptor_set_count,
                              VkDescriptorSetLayout *descriptor_set_layouts,
                              VkPipelineLayout *pipeline_layout) {

#ifdef DEBUG_RUNTIME
    debug(user_context)
        << " vk_create_pipeline_layout (user_context: " << user_context << ", "
        << "allocator: " << (void *)allocator << ", "
        << "descriptor_set_count: " << descriptor_set_count << ", "
        << "descriptor_set_layouts: " << (void *)descriptor_set_layouts << ", "
        << "pipeline_layout: " << (void *)pipeline_layout << ")\n";
#endif
    if (allocator == nullptr) {
        error(user_context) << "Vulkan: Failed to create pipeline layout ... invalid allocator pointer!";
        return halide_error_code_generic_error;
    }

    if (allocator->current_physical_device_limits().maxBoundDescriptorSets > 0) {
        uint64_t max_bound_descriptor_sets = allocator->current_physical_device_limits().maxBoundDescriptorSets;
        if (descriptor_set_count > max_bound_descriptor_sets) {
            error(user_context) << "Vulkan: Number of descriptor sets for pipeline layout exceeds the number that can be bound by device!\n"
                                << " requested: " << descriptor_set_count << ","
                                << " available: " << max_bound_descriptor_sets;
            return halide_error_code_incompatible_device_interface;
        }
    }

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
        vk_report_error(user_context, result, "vkCreatePipelineLayout");
        return halide_error_code_generic_error;
    }
    return halide_error_code_success;
}

int vk_destroy_pipeline_layout(void *user_context,
                               VulkanMemoryAllocator *allocator,
                               VkPipelineLayout pipeline_layout) {

#ifdef DEBUG_RUNTIME
    debug(user_context)
        << " vk_destroy_pipeline_layout (user_context: " << user_context << ", "
        << "allocator: " << (void *)allocator << ", "
        << "pipeline_layout: " << (void *)pipeline_layout << ")\n";
#endif

    if (allocator == nullptr) {
        error(user_context) << "Vulkan: Failed to destroy pipeline layout ... invalid allocator pointer!";
        return halide_error_code_generic_error;
    }

    vkDestroyPipelineLayout(allocator->current_device(), pipeline_layout, allocator->callbacks());
    return halide_error_code_success;
}

// --

int vk_create_compute_pipeline(void *user_context,
                               VulkanMemoryAllocator *allocator,
                               const char *pipeline_name,
                               VkShaderModule shader_module,
                               VkPipelineLayout pipeline_layout,
                               VkSpecializationInfo *specialization_info,
                               VkPipeline *compute_pipeline) {

#ifdef DEBUG_RUNTIME
    debug(user_context)
        << " vk_create_compute_pipeline (user_context: " << user_context << ", "
        << "allocator: " << (void *)allocator << ", "
        << "pipeline_name: " << pipeline_name << ", "
        << "shader_module: " << (void *)shader_module << ", "
        << "pipeline_layout: " << (void *)pipeline_layout << ")\n";
#endif
    if (allocator == nullptr) {
        error(user_context) << "Vulkan: Failed to create compute pipeline ... invalid allocator pointer!";
        return halide_error_code_generic_error;
    }

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
                specialization_info,                                  // pointer to VkSpecializationInfo struct
            },
            pipeline_layout,  // pipeline layout
            VK_NULL_HANDLE,   // base pipeline handle for derived pipeline
            0                 // base pipeline index for derived pipeline
        };

    VkResult result = vkCreateComputePipelines(allocator->current_device(), VK_NULL_HANDLE, 1, &compute_pipeline_info, allocator->callbacks(), compute_pipeline);
    if (result != VK_SUCCESS) {
        vk_report_error(user_context, result, "vkCreateComputePipeline")
            << "failed to create compute pipeline " << pipeline_name << ".\n"
            << " (This might be a bug in Halide. To debug this, see the HL_SPIRV_DUMP_FILE environment variable, and use the Khronos validator to make a bug report)";

        return halide_error_code_generic_error;
    }

    return halide_error_code_success;
}

int vk_setup_compute_pipeline(void *user_context,
                              VulkanMemoryAllocator *allocator,
                              VulkanShaderBinding *shader_bindings,
                              VulkanDispatchData *dispatch_data,
                              VkShaderModule shader_module,
                              VkPipelineLayout pipeline_layout,
                              VkPipeline *compute_pipeline) {

#ifdef DEBUG_RUNTIME
    debug(user_context)
        << " vk_setup_compute_pipeline (user_context: " << user_context << ", "
        << "entry_point_name: '" << shader_bindings->entry_point_name << "', "
        << "allocator: " << (void *)allocator << ", "
        << "shader_bindings: " << (void *)shader_bindings << ", "
        << "dispatch_data: " << (void *)dispatch_data << ", "
        << "shader_module: " << (void *)shader_module << ", "
        << "pipeline_layout: " << (void *)pipeline_layout << ")\n";
#endif

    if (allocator == nullptr) {
        error(user_context) << "Vulkan: Failed to setup compute pipeline ... invalid allocator pointer!";
        return halide_error_code_generic_error;
    }

    if (shader_bindings == nullptr) {
        error(user_context) << "Vulkan: Failed to setup compute pipeline ... invalid shader bindings!";
        return halide_error_code_generic_error;
    }

    if (shader_bindings == nullptr) {
        error(user_context) << "Vulkan: Failed to setup compute pipeline ... invalid dispatch data!";
        return halide_error_code_generic_error;
    }

    VkResult result = VK_SUCCESS;
    const char *entry_point_name = shader_bindings->entry_point_name;
    if (entry_point_name == nullptr) {
        error(user_context) << "Vulkan: Failed to setup compute pipeline ... missing entry point name!";
        return halide_error_code_generic_error;
    }

    uint32_t dispatch_constant_index = 0;
    uint32_t dispatch_constant_ids[4] = {0, 0, 0, 0};
    uint32_t dispatch_constant_values[4] = {0, 0, 0, 0};

    // locate the mapping for overriding any dynamic shared memory allocation sizes
    if (shader_bindings->shared_memory_allocations_count && dispatch_data->shared_mem_bytes) {

        uint32_t shared_mem_constant_id = 0;
        uint32_t static_shared_mem_bytes = 0;
        uint32_t shared_mem_type_size = 0;

        for (uint32_t sm = 0; sm < shader_bindings->shared_memory_allocations_count; sm++) {
            VulkanSharedMemoryAllocation *allocation = &(shader_bindings->shared_memory_allocations[sm]);
            if (allocation->constant_id == 0) {
                // static fixed-size allocation
                static_shared_mem_bytes += allocation->type_size * allocation->array_size;
            } else {
                // dynamic allocation
                if (shared_mem_constant_id > 0) {
                    error(user_context) << "Vulkan: Multiple dynamic shared memory allocations found! Only one is suported!!";
                    result = VK_ERROR_TOO_MANY_OBJECTS;
                    break;
                }
                shared_mem_constant_id = allocation->constant_id;
                shared_mem_type_size = allocation->type_size;
            }
        }
        uint32_t shared_mem_bytes_avail = (dispatch_data->shared_mem_bytes - static_shared_mem_bytes);
#ifdef DEBUG_RUNTIME
        debug(user_context) << "  pipeline uses " << static_shared_mem_bytes << " bytes of static shared memory\n";
        debug(user_context) << "  dispatch requests " << dispatch_data->shared_mem_bytes << " bytes of shared memory\n";
        debug(user_context) << "  dynamic shared memory " << shared_mem_bytes_avail << " bytes available\n";
#endif
        // setup the dynamic array size
        if ((shared_mem_constant_id > 0) && (shared_mem_bytes_avail > 0)) {
            uint32_t dynamic_array_size = (uint32_t)shared_mem_bytes_avail / shared_mem_type_size;
#ifdef DEBUG_RUNTIME
            debug(user_context) << "  setting shared memory to " << (uint32_t)dynamic_array_size << " elements "
                                << "(or " << (uint32_t)shared_mem_bytes_avail << " bytes)\n";
#endif
            // save the shared mem specialization constant in the first slot
            dispatch_constant_ids[dispatch_constant_index] = shared_mem_constant_id;
            dispatch_constant_values[dispatch_constant_index] = dynamic_array_size;
            dispatch_constant_index++;
        }

        // verify the device can actually support the necessary amount of shared memory requested
        if (allocator->current_physical_device_limits().maxComputeSharedMemorySize > 0) {
            uint64_t device_shared_mem_size = allocator->current_physical_device_limits().maxComputeSharedMemorySize;
            if (static_shared_mem_bytes > device_shared_mem_size) {
                error(user_context) << "Vulkan: Amount of static shared memory used exceeds device limit!\n"
                                    << " requested: " << static_shared_mem_bytes << " bytes,"
                                    << " available: " << device_shared_mem_size << " bytes";
                return halide_error_code_incompatible_device_interface;
            }
            if (dispatch_data->shared_mem_bytes > device_shared_mem_size) {
                error(user_context) << "Vulkan: Amount of dynamic shared memory used exceeds device limit!\n"
                                    << " requested: " << dispatch_data->shared_mem_bytes << " bytes,"
                                    << " available: " << device_shared_mem_size << " bytes";
                return halide_error_code_incompatible_device_interface;
            }
        }
    }

    // locate the mapping for overriding any dynamic workgroup local sizes
    if (shader_bindings->dispatch_data.local_size_binding.constant_id[0] != 0) {
        for (uint32_t dim = 0; dim < 3; dim++) {
            dispatch_constant_ids[dispatch_constant_index] = shader_bindings->dispatch_data.local_size_binding.constant_id[dim];
            dispatch_constant_values[dispatch_constant_index] = dispatch_data->local_size[dim];
            dispatch_constant_index++;
        }
    }

    // verify the specialization constants actually exist
    for (uint32_t dc = 0; dc < dispatch_constant_index; dc++) {
        const uint32_t invalid_index = uint32_t(-1);
        uint32_t found_index = invalid_index;
        for (uint32_t sc = 0; sc < shader_bindings->specialization_constants_count; sc++) {
            if (shader_bindings->specialization_constants[sc].constant_id == dispatch_constant_ids[dc]) {
#ifdef DEBUG_RUNTIME
                debug(user_context) << "  binding specialization constant [" << dispatch_constant_ids[dc] << "] "
                                    << "'" << shader_bindings->specialization_constants[sc].constant_name << "' "
                                    << " => " << dispatch_constant_values[dc] << "\n";
#endif
                found_index = sc;
                break;
            }
        }
        if (found_index == invalid_index) {
            error(user_context) << "Vulkan: Failed to locate dispatch constant index for shader binding!";
            result = VK_ERROR_INITIALIZATION_FAILED;
        }
    }

    // don't even attempt to create the pipeline layout if we encountered errors in the shader binding
    if (result != VK_SUCCESS) {
        error(user_context) << "Vulkan: Failed to decode shader bindings! " << vk_get_error_name(result);
        return halide_error_code_generic_error;
    }

    // Prepare specialization mapping for all dispatch constants
    uint32_t dispatch_constant_count = 0;
    VkSpecializationMapEntry specialization_map_entries[4];
    memset(specialization_map_entries, 0, 4 * sizeof(VkSpecializationMapEntry));
    for (uint32_t dc = 0; dc < dispatch_constant_index && dc < 4; dc++) {
        specialization_map_entries[dc].constantID = dispatch_constant_ids[dc];
        specialization_map_entries[dc].size = sizeof(uint32_t);
        specialization_map_entries[dc].offset = dc * sizeof(uint32_t);
        dispatch_constant_count++;
    }

    if (dispatch_constant_count > 0) {

        // Prepare specialization info block for the shader stage
        VkSpecializationInfo specialization_info{};
        specialization_info.dataSize = dispatch_constant_count * sizeof(uint32_t);
        specialization_info.mapEntryCount = dispatch_constant_count;
        specialization_info.pMapEntries = specialization_map_entries;
        specialization_info.pData = dispatch_constant_values;

        // Recreate the pipeline with the requested shared memory allocation
        if (shader_bindings->compute_pipeline) {
            int error_code = vk_destroy_compute_pipeline(user_context, allocator, shader_bindings->compute_pipeline);
            if (error_code != halide_error_code_success) {
                error(user_context) << "Vulkan: Failed to destroy compute pipeline!";
                return halide_error_code_generic_error;
            }
            shader_bindings->compute_pipeline = VK_NULL_HANDLE;
        }

        int error_code = vk_create_compute_pipeline(user_context, allocator, entry_point_name, shader_module, pipeline_layout, &specialization_info, &(shader_bindings->compute_pipeline));
        if (error_code != halide_error_code_success) {
            error(user_context) << "Vulkan: Failed to create compute pipeline!";
            return error_code;
        }

    } else {

        // Construct and re-use the fixed pipeline
        if (shader_bindings->compute_pipeline == VK_NULL_HANDLE) {
            int error_code = vk_create_compute_pipeline(user_context, allocator, entry_point_name, shader_module, pipeline_layout, nullptr, &(shader_bindings->compute_pipeline));
            if (error_code != halide_error_code_success) {
                error(user_context) << "Vulkan: Failed to create compute pipeline!";
                return error_code;
            }
        }
    }

    return halide_error_code_success;
}

int vk_destroy_compute_pipeline(void *user_context,
                                VulkanMemoryAllocator *allocator,
                                VkPipeline compute_pipeline) {
#ifdef DEBUG_RUNTIME
    debug(user_context)
        << " vk_destroy_compute_pipeline (user_context: " << user_context << ", "
        << "allocator: " << (void *)allocator << ", "
        << "device: " << (void *)allocator->current_device() << ", "
        << "compute_pipeline: " << (void *)compute_pipeline << ")\n";
#endif
    if (allocator == nullptr) {
        error(user_context) << "Vulkan: Failed to destroy compute pipeline ... invalid allocator pointer!";
        return halide_error_code_generic_error;
    }

    vkDestroyPipeline(allocator->current_device(), compute_pipeline, allocator->callbacks());
    return halide_error_code_success;
}

// --------------------------------------------------------------------------

VulkanShaderBinding *vk_decode_shader_bindings(void *user_context, VulkanMemoryAllocator *allocator, const uint32_t *module_ptr, uint32_t module_size) {
#ifdef DEBUG_RUNTIME
    debug(user_context)
        << " vk_decode_shader_bindings (user_context: " << user_context << ", "
        << "allocator: " << (void *)allocator << ", "
        << "module_ptr: " << (void *)module_ptr << ", "
        << "module_size: " << module_size << ")\n";

    uint64_t t_before = halide_current_time_ns(user_context);
#endif

    if (allocator == nullptr) {
        error(user_context) << "Vulkan: Failed to decode shader bindings ... invalid allocator pointer!";
        return nullptr;
    }

    if ((module_ptr == nullptr) || (module_size < (2 * sizeof(uint32_t)))) {
        error(user_context) << "Vulkan: Failed to decode shader bindings ... invalid module buffer!";
        return nullptr;
    }

    // Decode the sidecar for the module that lists the descriptor sets
    // corresponding to each entry point contained in the module.
    //
    // Construct a shader binding for each entry point that defines all
    // the buffers, constants, shared memory, and workgroup sizes
    // that are required for execution.
    //
    // Like the SPIR-V code module, each entry is one word (1x uint32_t).
    // Variable length sections are prefixed with their length (ie number of entries).
    //
    // [0] Header word count (total length of header)
    // [1] Number of descriptor sets
    // ... For each descriptor set ...
    // ... [0] Length of entry point name (padded to nearest word size)
    // ....... [*] Entry point string data (padded with null chars)
    // ... [1] Number of uniform buffers for this descriptor set
    // ... [2] Number of storage buffers for this descriptor set
    // ... [3] Number of specialization constants for this descriptor set
    // ....... For each specialization constant ...
    // ....... [0] Length of constant name string (padded to nearest word size)
    // ........... [*] Constant name string data (padded with null chars)
    // ....... [1] Constant id (as used in VkSpecializationMapEntry for binding)
    // ....... [2] Size of data type (in bytes)
    // ... [4] Number of shared memory allocations for this descriptor set
    // ....... For each allocation ...
    // ....... [0] Length of variable name string (padded to nearest word size)
    // ........... [*] Variable name string data (padded with null chars)
    // ....... [1] Constant id to use for overriding array size (zero if it is not bound to a specialization constant)
    // ....... [2] Size of data type (in bytes)
    // ....... [3] Size of array (ie element count)
    // ... [4] Dynamic workgroup dimensions bound to specialization constants
    // ....... [0] Constant id to use for local_size_x (zero if it was statically declared and not bound to a specialization constant)
    // ....... [1] Constant id to use for local_size_y
    // ....... [2] Constant id ot use for local_size_z
    //
    // NOTE: See CodeGen_Vulkan_Dev::SPIRV_Emitter::encode_header() for the encoding
    //
    // Both vk_decode_shader_bindings() and vk_compile_shader_module() will
    // need to be updated if the header encoding ever changes!
    //
    uint32_t module_entries = module_size / sizeof(uint32_t);
    uint32_t idx = 1;  // skip past the header_word_count
    uint32_t shader_count = module_ptr[idx++];
    if (shader_count < 1) {
        error(user_context) << "Vulkan: Failed to decode shader bindings ... no descriptors found!";
        return nullptr;  // no descriptors
    }

    // allocate an array of shader bindings (one for each entry point in the module)
    VkSystemAllocationScope alloc_scope = VkSystemAllocationScope::VK_SYSTEM_ALLOCATION_SCOPE_OBJECT;
    size_t shader_bindings_size = shader_count * sizeof(VulkanShaderBinding);
    VulkanShaderBinding *shader_bindings = (VulkanShaderBinding *)vk_host_malloc(user_context, shader_bindings_size, 0, alloc_scope, allocator->callbacks());
    if (shader_bindings == nullptr) {
        error(user_context) << "Vulkan: Failed to allocate shader_bindings! Out of memory!";
        return nullptr;
    }
    memset(shader_bindings, 0, shader_bindings_size);

    // decode and fill in the shader binding for each entry point
    for (uint32_t n = 0; (n < shader_count) && (idx < module_entries); n++) {
        halide_debug_assert(user_context, (idx + 8) < module_entries);  // should be at least 8 entries

        // [0] Length of entry point name (padded to nearest word size)
        uint32_t entry_point_name_length = module_ptr[idx++];  // length is number of uint32_t entries

        // [*] Entry point string data (padded with null chars)
        const char *entry_point_name = (const char *)(module_ptr + idx);  // NOTE: module owns string data
        idx += entry_point_name_length;                                   // skip past string data

        // [1] Number of uniform buffers for this descriptor set
        uint32_t uniform_buffer_count = module_ptr[idx++];

        // [2] Number of storage buffers for this descriptor set
        uint32_t storage_buffer_count = module_ptr[idx++];

        // [3] Number of specialization constants for this descriptor set
        uint32_t specialization_constants_count = module_ptr[idx++];

        // Decode all specialization constants
        VulkanSpecializationConstant *specialization_constants = nullptr;
        if (specialization_constants_count > 0) {

            // Allocate an array to store the decoded specialization constant data
            size_t specialization_constants_size = specialization_constants_count * sizeof(VulkanSpecializationConstant);
            specialization_constants = (VulkanSpecializationConstant *)vk_host_malloc(user_context, specialization_constants_size, 0, alloc_scope, allocator->callbacks());
            if (specialization_constants == nullptr) {
                error(user_context) << "Vulkan: Failed to allocate specialization_constants! Out of memory!";
                return nullptr;
            }
            memset(specialization_constants, 0, specialization_constants_size);

            // For each specialization constant ...
            for (uint32_t sc = 0; sc < specialization_constants_count; sc++) {
                halide_debug_assert(user_context, (idx + 4) < module_entries);  // should be at least 4 entries

                // [0] Length of constant name string (padded to nearest word size)
                uint32_t constant_name_length = module_ptr[idx++];

                // [*] Constant name string data (padded with null chars)
                const char *constant_name = (const char *)(module_ptr + idx);
                specialization_constants[sc].constant_name = constant_name;  // NOTE: module owns string data
                idx += constant_name_length;                                 // skip past string data

                // [1] Constant id (as used in VkSpecializationMapEntry for binding)
                specialization_constants[sc].constant_id = module_ptr[idx++];

                // [2] Size of data type (in bytes)
                specialization_constants[sc].type_size = module_ptr[idx++];
            }
        }

        // [4] Number of shared memory allocations for this descriptor set
        uint32_t shared_memory_allocations_count = module_ptr[idx++];  // [3]

        // Decode all shared memory allocations ...
        VulkanSharedMemoryAllocation *shared_memory_allocations = nullptr;
        if (shared_memory_allocations_count > 0) {

            // Allocate an array to store the decoded shared memory allocation data
            size_t shared_memory_allocations_size = shared_memory_allocations_count * sizeof(VulkanSharedMemoryAllocation);
            shared_memory_allocations = (VulkanSharedMemoryAllocation *)vk_host_malloc(user_context, shared_memory_allocations_size, 0, alloc_scope, allocator->callbacks());
            if (shared_memory_allocations == nullptr) {
                error(user_context) << "Vulkan: Failed to allocate shared_memory_allocations! Out of memory!";
                return nullptr;
            }
            memset(shared_memory_allocations, 0, shared_memory_allocations_size);

            // For each shared memory allocation ...
            for (uint32_t sm = 0; sm < shared_memory_allocations_count && (idx < module_entries); sm++) {
                halide_debug_assert(user_context, (idx + 4) < module_entries);  // should be at least 4 entries

                // [0] Length of variable name string (padded to nearest word size)
                uint32_t variable_name_length = module_ptr[idx++];

                // [*] Variable name string data (padded with null chars)
                const char *variable_name = (const char *)(module_ptr + idx);
                shared_memory_allocations[sm].variable_name = variable_name;  // NOTE: module owns string data
                idx += variable_name_length;                                  // skip past string data

                // [1] Constant id to use for overriding array size
                shared_memory_allocations[sm].constant_id = module_ptr[idx++];

                // [2] Size of data type (in bytes)
                shared_memory_allocations[sm].type_size = module_ptr[idx++];

                // [3] Size of array (ie element count)
                shared_memory_allocations[sm].array_size = module_ptr[idx++];
            }
        }

        // [4] Dynamic workgroup dimensions bound to specialization constants
        halide_debug_assert(user_context, (idx + 3) < module_entries);  // should be at least 3 entries
        for (uint32_t dim = 0; dim < 3 && (idx < module_entries); dim++) {
            shader_bindings[n].dispatch_data.local_size_binding.constant_id[dim] = module_ptr[idx++];
        }

#ifdef DEBUG_RUNTIME

        debug(user_context) << "  [" << n << "] '" << (const char *)entry_point_name << "'\n";

        debug(user_context) << "   uniform_buffer_count=" << uniform_buffer_count << "\n"
                            << "   storage_buffer_count=" << storage_buffer_count << "\n";

        debug(user_context) << "   specialization_constants_count=" << specialization_constants_count << "\n";
        for (uint32_t sc = 0; sc < specialization_constants_count; sc++) {
            debug(user_context) << "   [" << sc << "] "
                                << "constant_name='" << (const char *)specialization_constants[sc].constant_name << "' "
                                << "constant_id=" << specialization_constants[sc].constant_id << " "
                                << "type_size=" << specialization_constants[sc].type_size << "\n";
        }

        debug(user_context) << "   shared_memory_allocations_count=" << shared_memory_allocations_count << "\n";
        for (uint32_t sm = 0; sm < shared_memory_allocations_count; sm++) {
            debug(user_context) << "   [" << sm << "] "
                                << "variable_name='" << (const char *)shared_memory_allocations[sm].variable_name << "' "
                                << "constant_id=" << shared_memory_allocations[sm].constant_id << " "
                                << "type_size=" << shared_memory_allocations[sm].type_size << " "
                                << "array_size=" << shared_memory_allocations[sm].array_size << "\n";
        }
        debug(user_context) << "   local_size_binding=[";
        for (uint32_t dim = 0; dim < 3 && (idx < module_entries); dim++) {
            debug(user_context) << shader_bindings[n].dispatch_data.local_size_binding.constant_id[dim] << " ";
        }
        debug(user_context) << "]\n";
#endif
        shader_bindings[n].entry_point_name = (char *)vk_host_malloc(user_context, entry_point_name_length * sizeof(uint32_t), 0, alloc_scope, allocator->callbacks());
        if (shader_bindings[n].entry_point_name == nullptr) {
            error(user_context) << "Vulkan: Failed to allocate entry_point_name! Out of memory!";
            return nullptr;
        }

        memcpy(shader_bindings[n].entry_point_name, entry_point_name, entry_point_name_length * sizeof(uint32_t));
        shader_bindings[n].uniform_buffer_count = uniform_buffer_count;
        shader_bindings[n].storage_buffer_count = storage_buffer_count;
        shader_bindings[n].specialization_constants_count = specialization_constants_count;
        shader_bindings[n].specialization_constants = specialization_constants;
        shader_bindings[n].shared_memory_allocations_count = shared_memory_allocations_count;
        shader_bindings[n].shared_memory_allocations = shared_memory_allocations;
    }

#ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
#endif

    return shader_bindings;
}

int vk_validate_shader_for_device(void *user_context, VulkanMemoryAllocator *allocator,
                                  const VulkanShaderBinding *shader_bindings, uint32_t shader_count) {
#ifdef DEBUG_RUNTIME
    debug(user_context)
        << " vk_validate_shader_for_device (user_context: " << user_context << ", "
        << "allocator: " << (void *)allocator << ", "
        << "shader_bindings: " << (void *)shader_bindings << ", "
        << "shader_count: " << shader_count << ")\n";
#endif

    // validate that the shared memory used is less than the available amount on device
    if (shader_bindings->shared_memory_allocations_count) {

        uint32_t static_shared_mem_bytes = 0;

        for (uint32_t sm = 0; sm < shader_bindings->shared_memory_allocations_count; sm++) {
            VulkanSharedMemoryAllocation *allocation = &(shader_bindings->shared_memory_allocations[sm]);
            if (allocation->constant_id == 0) {
                // static fixed-size allocation
                static_shared_mem_bytes += allocation->type_size * allocation->array_size;
            } else {
                // dynamic allocation (can't determine this until runtime)
            }
        }

        // verify the device can actually support the necessary amount of shared memory requested
        if (allocator->current_physical_device_limits().maxComputeSharedMemorySize > 0) {
            uint64_t device_shared_mem_size = allocator->current_physical_device_limits().maxComputeSharedMemorySize;
            if (static_shared_mem_bytes > device_shared_mem_size) {
                error(user_context) << "Vulkan: Amount of static shared memory used exceeds device limit!\n"
                                    << " requested: " << static_shared_mem_bytes << " bytes,"
                                    << " available: " << device_shared_mem_size << " bytes";
                return halide_error_code_incompatible_device_interface;
            }
        }
    }

    // validate the number of descriptor sets used is within the amount supported by the device
    if (allocator->current_physical_device_limits().maxPerStageDescriptorStorageBuffers > 0) {
        uint64_t max_descriptors = allocator->current_physical_device_limits().maxPerStageDescriptorStorageBuffers;
        if (shader_count > max_descriptors) {
            error(user_context) << "Vulkan: Number of required descriptor sets exceeds the amount available for device!\n"
                                << " requested: " << shader_count << ","
                                << " available: " << max_descriptors;
            return halide_error_code_incompatible_device_interface;
        }
    }
    return halide_error_code_success;
}

VulkanCompilationCacheEntry *vk_compile_kernel_module(void *user_context, VulkanMemoryAllocator *allocator,
                                                      const char *ptr, int size) {
#ifdef DEBUG_RUNTIME
    debug(user_context)
        << " vk_compile_kernel_module (user_context: " << user_context << ", "
        << "allocator: " << (void *)allocator << ", "
        << "device: " << (void *)allocator->current_device() << ", "
        << "module: " << (void *)ptr << ", "
        << "size: " << size << ")\n";

    uint64_t t_before = halide_current_time_ns(user_context);
#endif

    if (allocator == nullptr) {
        debug(user_context) << "Vulkan: Failed to compile kernel module ... invalid allocator pointer!\n";
        return nullptr;
    }

    if ((ptr == nullptr) || (size <= 0)) {
        debug(user_context) << "Vulkan: Failed to compile kernel module ... invalid module!\n";
        return nullptr;
    }

    VkSystemAllocationScope alloc_scope = VkSystemAllocationScope::VK_SYSTEM_ALLOCATION_SCOPE_OBJECT;
    VulkanCompilationCacheEntry *cache_entry = (VulkanCompilationCacheEntry *)vk_host_malloc(user_context, sizeof(VulkanCompilationCacheEntry), 0, alloc_scope, allocator->callbacks());
    if (cache_entry == nullptr) {
        debug(user_context) << "Vulkan: Failed to allocate compilation cache entry! Out of memory!\n";
        return nullptr;
    }
    memset(cache_entry, 0, sizeof(VulkanCompilationCacheEntry));

    // Decode the header and the kernel modules
    uint32_t word_offset = 0;
    const uint32_t *module_header = (const uint32_t *)(ptr);
    if ((size_t)size < sizeof(uint32_t)) {
        debug(user_context) << "Vulkan: Code module size is invalid!\n";
        return nullptr;
    }

    // Extract the number of kernels from the module header
    uint32_t kernel_count = module_header[word_offset++];
    debug(user_context) << "  kernel_count=" << kernel_count << "\n";

    // Allocate enough space to store the compiled modules
    cache_entry->compiled_modules = (VulkanCompiledShaderModule **)vk_host_malloc(user_context, sizeof(VulkanCompiledShaderModule *) * kernel_count, 0, alloc_scope, allocator->callbacks());
    if (cache_entry->compiled_modules == nullptr) {
        debug(user_context) << "Vulkan: Failed to allocate host memory!\n";
        return nullptr;
    }
    cache_entry->module_count = kernel_count;
    cache_entry->allocator = allocator;

    // Allocate a temp buffer to decode the binary sizes of each "SPIR-V Module"
    uint32_t *binary_sizes = (uint32_t *)vk_system_malloc(user_context, sizeof(uint32_t) * kernel_count);
    if (binary_sizes == nullptr) {
        debug(user_context) << "Vulkan: Failed to allocate system memory!\n";
        return nullptr;
    }

    // Extract the size of each "SPIR-V Module" for each kernel
    size_t byte_offset = 0;
    for (uint32_t i = 0; (i < kernel_count) && (byte_offset < (size_t)size); ++i) {
        // Extract binary size
        binary_sizes[i] = module_header[word_offset++];

        // Skip past the kernel name
        uint32_t kernel_name_entry_size = module_header[word_offset++];
        const char *kernel_name = (const char *)(module_header + word_offset);
        word_offset += kernel_name_entry_size;

        // Compute byte offset for loop range check
        byte_offset = (word_offset * sizeof(uint32_t));
        debug(user_context) << "  kernel[" << i << "] name: " << kernel_name << " binary_size: " << binary_sizes[i] << " bytes\n";
    }

    // Compile each "SPIR-V Module" for each kernel
    halide_error_code_t error_code = halide_error_code_success;
    for (uint32_t i = 0; (i < kernel_count) && (byte_offset < (size_t)size); ++i) {

        // Skip the header and determine the start address of the "SPIR-V Module"
        const uint32_t *spirv_ptr = (const uint32_t *)(ptr + byte_offset);
        size_t spirv_size = binary_sizes[i];

        debug(user_context) << "  spirv_size[" << i << "] = " << spirv_size << " bytes\n";
        debug(user_context) << "  spirv_ptr[" << i << "] = " << spirv_ptr << "\n";

        // Compile the "SPIR-V Module" for the kernel
        cache_entry->compiled_modules[i] = vk_compile_shader_module(user_context, allocator, (const char *)spirv_ptr, (int)spirv_size);
        if (cache_entry->compiled_modules[i] == nullptr) {
            debug(user_context) << "Vulkan: Failed to compile shader module!";
            error_code = halide_error_code_generic_error;
        }

        // Skip to the next "SPIR-V Module"
        byte_offset += binary_sizes[i];
    }

    // Free temp buffer
    vk_system_free(user_context, binary_sizes);

    // Cleanup if compile failed
    if (error_code != halide_error_code_success) {
        vk_host_free(user_context, cache_entry->compiled_modules, allocator->callbacks());
        vk_host_free(user_context, cache_entry, allocator->callbacks());
        cache_entry = nullptr;
    }

#ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
#endif

    return cache_entry;
}

VulkanCompiledShaderModule *vk_compile_shader_module(void *user_context, VulkanMemoryAllocator *allocator,
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

    if (allocator == nullptr) {
        error(user_context) << "Vulkan: Failed to compile shader modules ... invalid allocator pointer!";
        return nullptr;
    }

    if ((ptr == nullptr) || (size <= 0)) {
        error(user_context) << "Vulkan: Failed to compile shader modules ... invalid program source buffer!";
        return nullptr;
    }

    const uint32_t *module_ptr = (const uint32_t *)ptr;
    const uint32_t module_size = (const uint32_t)size;

    halide_debug_assert(user_context, module_ptr != nullptr);
    halide_debug_assert(user_context, module_size >= (2 * sizeof(uint32_t)));

    uint32_t header_word_count = module_ptr[0];
    uint32_t shader_count = module_ptr[1];
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
    VulkanCompiledShaderModule *compiled_module = (VulkanCompiledShaderModule *)vk_host_malloc(user_context, sizeof(VulkanCompiledShaderModule), 0, alloc_scope, allocator->callbacks());
    if (compiled_module == nullptr) {
        error(user_context) << "Vulkan: Failed to allocate compilation cache entry! Out of memory!";
        return nullptr;
    }
    memset(compiled_module, 0, sizeof(VulkanCompiledShaderModule));

    // decode the entry point data and extract the shader bindings
    VulkanShaderBinding *decoded_bindings = vk_decode_shader_bindings(user_context, allocator, module_ptr, module_size);
    if (decoded_bindings == nullptr) {
        error(user_context) << "Vulkan: Failed to decode shader bindings!";
        return nullptr;
    }

    // validate that the compiled shader can be executed by the device with the requested resources
    int valid_status = vk_validate_shader_for_device(user_context, allocator, decoded_bindings, shader_count);
    if (valid_status != halide_error_code_success) {
        vk_host_free(user_context, compiled_module->shader_bindings, allocator->callbacks());
        vk_host_free(user_context, compiled_module, allocator->callbacks());
        return nullptr;
    }

    // save the shader bindings in the cache entry
    compiled_module->shader_bindings = decoded_bindings;
    compiled_module->shader_count = shader_count;

    VkResult result = vkCreateShaderModule(allocator->current_device(), &shader_info, allocator->callbacks(), &compiled_module->shader_module);
    if (result != VK_SUCCESS) {
        vk_report_error(user_context, result, "vkCreateShaderModule");
        vk_host_free(user_context, compiled_module->shader_bindings, allocator->callbacks());
        vk_host_free(user_context, compiled_module, allocator->callbacks());
        return nullptr;
    }

    // allocate an array for storing the descriptor set layouts
    if (compiled_module->shader_count) {
        compiled_module->descriptor_set_layouts = (VkDescriptorSetLayout *)vk_host_malloc(user_context, compiled_module->shader_count * sizeof(VkDescriptorSetLayout), 0, alloc_scope, allocator->callbacks());
        if (compiled_module->descriptor_set_layouts == nullptr) {
            error(user_context) << "Vulkan: Failed to allocate descriptor set layouts for cache entry! Out of memory!";
            return nullptr;
        }
        memset(compiled_module->descriptor_set_layouts, 0, compiled_module->shader_count * sizeof(VkDescriptorSetLayout));
    }

#ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
#endif

    return compiled_module;
}

void vk_destroy_compiled_shader_module(VulkanCompiledShaderModule *shader_module, VulkanMemoryAllocator *allocator) {
    void *user_context = nullptr;
#ifdef DEBUG_RUNTIME
    debug(user_context)
        << " vk_destroy_compiled_shader_module (shader_module: "
        << shader_module << ", allocator: " << allocator << ")\n";
#endif

    if (shader_module == nullptr) {
        return;
    }

    if (allocator == nullptr) {
        return;
    }

    if (shader_module->descriptor_set_layouts) {
        for (uint32_t n = 0; n < shader_module->shader_count; n++) {
            debug(user_context) << "  destroying descriptor set layout [" << n << "] " << shader_module->descriptor_set_layouts[n] << "\n";
            vk_destroy_descriptor_set_layout(user_context, allocator, shader_module->descriptor_set_layouts[n]);
            shader_module->descriptor_set_layouts[n] = VK_NULL_HANDLE;
        }
        debug(user_context) << "  destroying descriptor set layout " << (void *)shader_module->descriptor_set_layouts << "\n";
        vk_host_free(user_context, shader_module->descriptor_set_layouts, allocator->callbacks());
        shader_module->descriptor_set_layouts = nullptr;
    }
    if (shader_module->pipeline_layout) {
        debug(user_context) << "  destroying pipeline layout " << (void *)shader_module->pipeline_layout << "\n";
        vk_destroy_pipeline_layout(user_context, allocator, shader_module->pipeline_layout);
        shader_module->pipeline_layout = VK_NULL_HANDLE;
    }

    if (shader_module->shader_bindings) {
#ifdef DEBUG_RUNTIME
        debug(user_context)
            << "  destroying shader bindings ("
            << "shader_module: " << shader_module << ", "
            << "shader_bindings: " << shader_module->shader_bindings << ")\n";
#endif
        for (uint32_t n = 0; n < shader_module->shader_count; n++) {
            debug(user_context) << "  destroying shader binding [" << n << "] ";
            if (shader_module->shader_bindings[n].entry_point_name) {
                debug(user_context) << shader_module->shader_bindings[n].entry_point_name << "\n";
                vk_host_free(user_context, shader_module->shader_bindings[n].entry_point_name, allocator->callbacks());
                shader_module->shader_bindings[n].entry_point_name = nullptr;
            } else {
                debug(user_context) << "<unknown entry point>\n";
            }
            if (shader_module->shader_bindings[n].args_region) {
                debug(user_context) << "  destroying shader binding args regions [" << n << "]\n";
                vk_destroy_scalar_uniform_buffer(user_context, allocator, shader_module->shader_bindings[n].args_region);
                shader_module->shader_bindings[n].args_region = nullptr;
            }
            if (shader_module->shader_bindings[n].descriptor_pool) {
                debug(user_context) << "  destroying shader binding descriptor pool [" << n << "]\n";
                vk_destroy_descriptor_pool(user_context, allocator, shader_module->shader_bindings[n].descriptor_pool);
                shader_module->shader_bindings[n].descriptor_pool = VK_NULL_HANDLE;
            }
            if (shader_module->shader_bindings[n].specialization_constants) {
                debug(user_context) << "  destroying shader binding specialization constants [" << n << "]\n";
                vk_host_free(user_context, shader_module->shader_bindings[n].specialization_constants, allocator->callbacks());
                shader_module->shader_bindings[n].specialization_constants = nullptr;
            }
            if (shader_module->shader_bindings[n].shared_memory_allocations) {
                debug(user_context) << "  destroying shader binding shared memory allocations [" << n << "]\n";
                vk_host_free(user_context, shader_module->shader_bindings[n].shared_memory_allocations, allocator->callbacks());
                shader_module->shader_bindings[n].shared_memory_allocations = nullptr;
            }
            if (shader_module->shader_bindings[n].compute_pipeline) {
                debug(user_context) << "  destroying shader binding compute pipeline [" << n << "]\n";
                vk_destroy_compute_pipeline(user_context, allocator, shader_module->shader_bindings[n].compute_pipeline);
                shader_module->shader_bindings[n].compute_pipeline = VK_NULL_HANDLE;
            }
        }
        vk_host_free(user_context, shader_module->shader_bindings, allocator->callbacks());
        shader_module->shader_bindings = nullptr;
    }
    if (shader_module->shader_module) {
        debug(user_context) << "   destroying shader module " << (void *)shader_module->shader_module << "\n";
        vkDestroyShaderModule(allocator->current_device(), shader_module->shader_module, allocator->callbacks());
        shader_module->shader_module = VK_NULL_HANDLE;
    }
    shader_module->shader_count = 0;
    vk_host_free(user_context, shader_module, allocator->callbacks());
    shader_module = nullptr;
    debug(user_context) << " Destroyed compiled shader module: " << (void *)shader_module << "\n";
}

void vk_destroy_compilation_cache_entry(VulkanCompilationCacheEntry *cache_entry) {
    void *user_context = nullptr;
    debug(user_context)
        << " vk_destroy_compilation_cache_entry (cache_entry: " << cache_entry << ")\n";

    if (cache_entry == nullptr) {
        return;
    }

    VulkanMemoryAllocator *allocator = cache_entry->allocator;
    if (allocator == nullptr) {
        return;
    }

    debug(user_context)
        << " Destroying " << cache_entry->module_count << " shader modules for cache entry (cache_entry: " << cache_entry << ")\n";

    for (uint32_t m = 0; m < cache_entry->module_count; m++) {
        debug(user_context)
            << " destroying compiled_module[" << m << "]: " << cache_entry->compiled_modules[m] << "\n";

        VulkanCompiledShaderModule *compiled_module = cache_entry->compiled_modules[m];
        vk_destroy_compiled_shader_module(compiled_module, allocator);
    }

    cache_entry->module_count = 0;
    cache_entry->allocator = nullptr;
    vk_host_free(user_context, cache_entry, allocator->callbacks());
    cache_entry = nullptr;
    debug(user_context) << "Vulkan: Destroyed compilation cache entry (cache_entry: " << cache_entry << ")\n";
}

int vk_destroy_shader_modules(void *user_context, VulkanMemoryAllocator *allocator) {

    debug(user_context)
        << " vk_destroy_shader_modules (user_context: " << user_context << ")\n";

#ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
#endif
    if (allocator != nullptr) {
        compilation_cache.delete_context(user_context, allocator->current_device(), vk_destroy_compilation_cache_entry);
    }

#ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
#endif
    return halide_error_code_success;
}

// --------------------------------------------------------------------------

int vk_do_multidimensional_copy(void *user_context, VkCommandBuffer command_buffer,
                                const device_copy &c, uint64_t src_offset, uint64_t dst_offset,
                                int d, bool from_host, bool to_host) {
    if (d == 0) {

        if ((!from_host && to_host) ||
            (from_host && !to_host) ||
            (!from_host && !to_host)) {

            VkBufferCopy buffer_copy = {
                src_offset,   // srcOffset
                dst_offset,   // dstOffset
                c.chunk_size  // size
            };

            VkBuffer *src_buffer = reinterpret_cast<VkBuffer *>(c.src);
            VkBuffer *dst_buffer = reinterpret_cast<VkBuffer *>(c.dst);
            if (!src_buffer || !dst_buffer) {
                error(user_context) << "Vulkan: Failed to retrieve buffer for device memory!";
                return halide_error_code_internal_error;
            }

            vkCmdCopyBuffer(command_buffer, *src_buffer, *dst_buffer, 1, &buffer_copy);

        } else if ((c.dst + dst_offset) != (c.src + src_offset)) {
            // Could reach here if a user called directly into the
            // Vulkan API for a device->host copy on a source buffer
            // with device_dirty = false.
            memcpy((void *)(c.dst + dst_offset), (void *)(c.src + src_offset), c.chunk_size);
        }
    } else {
        // TODO: deal with negative strides. Currently the code in
        // device_buffer_utils.h does not do so either.
        uint64_t src_off = 0, dst_off = 0;
        for (uint64_t i = 0; i < c.extent[d - 1]; i++) {
            int err = vk_do_multidimensional_copy(user_context, command_buffer, c,
                                                  src_offset + src_off,
                                                  dst_offset + dst_off,
                                                  d - 1, from_host, to_host);
            dst_off += c.dst_stride_bytes[d - 1];
            src_off += c.src_stride_bytes[d - 1];
            if (err) {
                return err;
            }
        }
    }
    return halide_error_code_success;
}

int vk_device_crop_from_offset(void *user_context,
                               const struct halide_buffer_t *src,
                               int64_t offset,
                               struct halide_buffer_t *dst) {

    VulkanContext ctx(user_context);
    if (ctx.error != halide_error_code_success) {
        error(user_context) << "Vulkan: Failed to acquire context!";
        return ctx.error;
    }

#ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
#endif

    if (offset < 0) {
        error(user_context) << "Vulkan: Invalid offset for device crop!";
        return halide_error_code_device_crop_failed;
    }

    // get the allocated region for the device
    MemoryRegion *device_region = reinterpret_cast<MemoryRegion *>(src->device);
    if (device_region == nullptr) {
        error(user_context) << "Vulkan: Failed to crop region! Invalide device region!";
        return halide_error_code_device_crop_failed;
    }

    // create the croppeg region from the allocated region
    MemoryRegion *cropped_region = ctx.allocator->create_crop(user_context, device_region, (uint64_t)offset);
    if ((cropped_region == nullptr) || (cropped_region->handle == nullptr)) {
        error(user_context) << "Vulkan: Failed to crop region! Unable to create memory region!";
        return halide_error_code_device_crop_failed;
    }

    // update the destination to the cropped region
    dst->device = (uint64_t)cropped_region;
    dst->device_interface = src->device_interface;

#ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
#endif

    return halide_error_code_success;
}

// --------------------------------------------------------------------------

}  // namespace
}  // namespace Vulkan
}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

#endif  // HALIDE_RUNTIME_VULKAN_RESOURCES_H
