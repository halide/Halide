#include "HalideRuntimeVulkan.h"

#include "device_buffer_utils.h"
#include "device_interface.h"
#include "runtime_internal.h"
#include "vulkan_context.h"
#include "vulkan_extensions.h"
#include "vulkan_internal.h"
#include "vulkan_memory.h"

using namespace Halide::Runtime::Internal::Vulkan;

// --

extern "C" {

// The default implementation of halide_acquire_vulkan_context uses
// the global pointers above, and serializes access with a spin lock.
// Overriding implementations of acquire/release must implement the
// following behavior:

//  - halide_acquire_vulkan_context should always store a valid
//   instance/device/queue in the corresponding out parameters,
//   or return an error code.
// - A call to halide_acquire_vulkan_context is followed by a matching
//   call to halide_release_vulkan_context. halide_acquire_vulkan_context
//   should block while a previous call (if any) has not yet been
//   released via halide_release_vulkan_context.
WEAK int halide_vulkan_acquire_context(void *user_context, 
                                       VkInstance *instance,
                                       VkDevice *device, VkQueue *queue, 
                                       VkPhysicalDevice *physical_device,
                                       uint32_t *queue_family_index, 
                                       VkAllocationCallbacks **allocation_callbacks,
                                       bool create) {

    halide_abort_if_false(user_context, instance != nullptr);
    halide_abort_if_false(user_context, device != nullptr);
    halide_abort_if_false(user_context, queue != nullptr);
    halide_abort_if_false(user_context, &thread_lock != nullptr);
    while (__atomic_test_and_set(&thread_lock, __ATOMIC_ACQUIRE)) {}

    // If the context has not been initialized, initialize it now.
    halide_abort_if_false(user_context, &cached_instance != nullptr);
    halide_abort_if_false(user_context, &cached_device != nullptr);
    halide_abort_if_false(user_context, &cached_queue != nullptr);
    halide_abort_if_false(user_context, &cached_physical_device != nullptr);
    if ((cached_instance == nullptr) && create) {
        int result = vk_create_context(user_context, 
            &cached_instance, 
            &cached_device, 
            &cached_queue, 
            &cached_physical_device, 
            &cached_queue_family_index, 
            &cached_allocation_callbacks
        );
        if (result != halide_error_code_success) {
            __atomic_clear(&thread_lock, __ATOMIC_RELEASE);
            return result;
        }
    }

    *allocation_callbacks = cached_allocation_callbacks;
    *instance = cached_instance;
    *device = cached_device;
    *queue = cached_queue;
    *physical_device = cached_physical_device;
    *queue_family_index = cached_queue_family_index;
    return 0;
}

WEAK int halide_vulkan_release_context(void *user_context, VkInstance instance, VkDevice device, VkQueue queue) {
    __atomic_clear(&thread_lock, __ATOMIC_RELEASE);
    return 0;
}

WEAK int halide_vulkan_device_free(void *user_context, halide_buffer_t *buf) {
    // halide_vulkan_device_free, at present, can be exposed to clients and they
    // should be allowed to call halide_vulkan_device_free on any halide_buffer_t
    // including ones that have never been used with a GPU.
    if (buf->device == 0) {
        return 0;
    }

    VulkanContext context(user_context);

#ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
#endif

    VkBuffer vk_buffer = (VkBuffer)buf->device;
    vkDestroyBuffer(context.device, vk_buffer, context.allocation_callbacks());

    buf->device = 0;
    buf->device_interface->impl->release_module();
    buf->device_interface = nullptr;

#ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
#endif

    return 0;
}

WEAK int halide_vulkan_initialize_kernels(void *user_context, void **state_ptr, const char *src, int size) {
    debug(user_context)
        << "Vulkan: halide_vulkan_init_kernels (user_context: " << user_context
        << ", state_ptr: " << state_ptr
        << ", program: " << (void *)src
        << ", size: " << size << "\n";

    VulkanContext ctx(user_context);
    if (ctx.error != VK_SUCCESS) {
        return ctx.error;
    }

#ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
#endif

    debug(user_context) << "halide_vulkan_initialize_kernels got compilation_cache mutex.\n";
    VkShaderModule* shader_module = nullptr;
    if (!compilation_cache.kernel_state_setup(user_context, state_ptr, ctx.device, shader_module,
                                              Halide::Runtime::Internal::Vulkan::vk_compile_shader_module, 
                                              user_context, ctx.device, src, size, ctx.allocation_callbacks())) {
        return halide_error_code_generic_error;
    }

#ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
#endif

    return 0;
}

WEAK void halide_vulkan_finalize_kernels(void *user_context, void *state_ptr) {
    debug(user_context)
        << "Vulkan: halide_vulkan_finalize_kernels (user_context: " << user_context
        << ", state_ptr: " << state_ptr << "\n";
    VulkanContext ctx(user_context);
    if (ctx.error == VK_SUCCESS) {
        compilation_cache.release_hold(user_context, ctx.device, state_ptr);
    }
}

// Used to generate correct timings when tracing
WEAK int halide_vulkan_device_sync(void *user_context, halide_buffer_t *) {
    debug(user_context) << "Vulkan: halide_vulkan_device_sync (user_context: " << user_context << ")\n";

    VulkanContext ctx(user_context);
    halide_debug_assert(user_context, ctx.error == VK_SUCCESS);

#ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
#endif

#ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
#endif

    return VK_SUCCESS;
}

WEAK int halide_vulkan_device_release(void *user_context) {
    debug(user_context)
        << "Vulkan: halide_vulkan_device_release (user_context: " << user_context << ")\n";

    VkInstance instance;
    VkDevice device;
    VkQueue queue;
    VkPhysicalDevice physical_device;
    VkAllocationCallbacks* alloc = nullptr;
    uint32_t _throwaway;
    int acquire_status = halide_vulkan_acquire_context(user_context, &instance, &device, &queue, &physical_device, &_throwaway, &alloc, false);
    halide_debug_assert(user_context, acquire_status == VK_SUCCESS);
    (void)acquire_status;
    if (instance != nullptr) {

        vkQueueWaitIdle(queue);
        vk_destroy_shader_modules(user_context, device, alloc);
        vk_destroy_memory_allocator(user_context);

        if (device == cached_device) {
            cached_device = nullptr;
            cached_physical_device = nullptr;
            cached_queue = nullptr;
        }
        vkDestroyDevice(device, nullptr);

        if (instance == cached_instance) {
            cached_instance = nullptr;
        }
        vkDestroyInstance(instance, nullptr);
        halide_vulkan_release_context(user_context, instance, device, queue);
    }

    return 0;
}

namespace {

VkResult vk_allocate_device_memory(VkPhysicalDevice physical_device,
                                   VkDevice device, VkDeviceSize size,
                                   VkMemoryPropertyFlags flags,
                                   const VkAllocationCallbacks *allocator,
                                   // returned in device_memory
                                   VkDeviceMemory *device_memory) {

#if 0
    MemoryRequest request = {0};
    request.size = halide_buffer->size_in_bytes();
    request.properties.usage = MemoryUsage::TransferSrc;
    request.properties.caching = MemoryCaching::DefaultCaching;
    request.properties.visibility = MemoryVisibility::HostToDevice;

    // allocate a new region -- acquires the context
    MemoryRegion* region = memory_allocator->reserve(user_context, request);
    if((region == nullptr) || (region->handle == nullptr)) {
        error(user_context) << "Vulkan: Failed to allocate device memory!\n";
        return halide_error_out_of_memory;
    }
#endif

    // Find an appropriate memory type given the flags
    auto memory_type_index = VK_MAX_MEMORY_TYPES;
    VkPhysicalDeviceMemoryProperties device_mem_properties;
    vkGetPhysicalDeviceMemoryProperties(physical_device, &device_mem_properties);

    // TODO: should this be host coherent or cached or something else?
    for (uint32_t i = 0; i < device_mem_properties.memoryTypeCount; i++) {
        if (device_mem_properties.memoryTypes[i].propertyFlags & flags) {
            memory_type_index = i;
            break;
        }
    }

    if (memory_type_index == VK_MAX_MEMORY_TYPES) {
        debug(nullptr) << "Vulkan: failed to find appropriate memory type";
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }

    // TODO: This can greatly benefit from, and is designed for, an
    // allocation cache.  We should consider allocating larger chunks
    // of memory and using the larger allocation (with appropriate size/offsets)
    // to back buffers created here

    VkMemoryAllocateInfo alloc_info =
        {
            VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,  // struct type
            nullptr,                                 // struct extending this
            size,                                    // size of allocation in bytes
            (uint32_t)memory_type_index              // memory type index from physical device
        };

    auto ret_code = vkAllocateMemory(device, &alloc_info, allocator, device_memory);

    if (ret_code != VK_SUCCESS) {
        debug(nullptr) << "Vulkan: vkAllocateMemory returned: " << get_vulkan_error_name(ret_code) << "\n";
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }

    return ret_code;
}

VkResult vk_create_command_pool(VkDevice device, uint32_t queue_index, const VkAllocationCallbacks *callbacks, VkCommandPool *command_pool) {

    VkCommandPoolCreateInfo command_pool_info =
        {
            VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,  // struct type
            nullptr,                                     // pointer to struct extending this
            0,                                           // flags.  may consider VK_COMMAND_POOL_CREATE_TRANSIENT_BIT
            queue_index                                  // queue family index corresponding to the compute command queue
        };
    return vkCreateCommandPool(device, &command_pool_info, callbacks, command_pool);
}

VkResult vk_create_command_buffer(VkDevice device, VkCommandPool pool, VkCommandBuffer *command_buffer) {

    VkCommandBufferAllocateInfo command_buffer_info =
        {
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,  // struct type
            nullptr,                                         // pointer to struct extending this
            pool,                                            // command pool for allocation
            VK_COMMAND_BUFFER_LEVEL_PRIMARY,                 // command buffer level
            1                                                // number to allocate
        };

    return vkAllocateCommandBuffers(device, &command_buffer_info, command_buffer);
}

}  // anonymous namespace

WEAK int halide_vulkan_device_malloc(void *user_context, halide_buffer_t *buf) {
    debug(user_context)
        << "halide_vulkan_device_malloc (user_context: " << user_context
        << ", buf: " << buf << ")\n";

    VulkanContext context(user_context);
    if (context.error != VK_SUCCESS) {
        return -1;
    }

    size_t size = buf->size_in_bytes();
    halide_debug_assert(user_context, size != 0);
    if (buf->device) {
        return 0;
    }

    for (int i = 0; i < buf->dimensions; i++) {
        halide_debug_assert(user_context, buf->dim[i].stride >= 0);
    }

    debug(user_context) << "    allocating " << *buf << "\n";

#ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
#endif

    // In Vulkan, we need to go through the following steps in order
    // to set up a device allocation for a Halide buffer:
    // 1. Allocate memory that backs the buffer.  This needs to be
    //    the appropriate memory type with the appropriate properties
    // 2. Construct a VkBuffer
    // 3. Bind the VkBuffer to the allocated memory

    // Allocate memory
    // TODO: This really needs an allocation cache
    VkDeviceMemory device_memory;
    auto ret_code = vk_allocate_device_memory(context.physical_device, context.device,
                                              size,
                                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                              context.allocation_callbacks(),
                                              &device_memory);

    VkBufferCreateInfo args_info = {
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr,
        0,
        size,
        // TODO: verify next flags
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_SHARING_MODE_EXCLUSIVE,
        0, nullptr};
    VkBuffer result;
    ret_code = vkCreateBuffer(context.device, &args_info, nullptr, &result);
    if (ret_code != VK_SUCCESS) {
        debug(user_context) << "Vulkan: vkCreateBuffer returned: " << get_vulkan_error_name(ret_code) << "\n";
        return -1;
    }

    // Finally, bind buffer
    ret_code = vkBindBufferMemory(context.device, result, device_memory, 0);

    if (ret_code != VK_SUCCESS) {
        debug(user_context) << "Vulkan: vkBindBufferMemory returned: " << get_vulkan_error_name(ret_code) << "\n";
        return -1;
    }

    buf->device = (uint64_t)result;
    buf->device_interface = &vulkan_device_interface;
    buf->device_interface->impl->use_module();

    debug(user_context)
        << "    Allocated device buffer " << (void *)buf->device
        << " for buffer " << buf << "\n";

#ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
#endif

    return 0;
}

namespace {

WEAK int do_multidimensional_copy(void *user_context, const VulkanContext &ctx,
                                  const device_copy &c,
                                  uint64_t off, int d, bool d_to_h) {
    if (d > MAX_COPY_DIMS) {
        error(user_context) << "Buffer has too many dimensions to copy to/from GPU\n";
        return -1;
    } else if (d == 0) {
        void vkCmdCopyBuffer(
            VkCommandBuffer commandBuffer,
            VkBuffer srcBuffer,
            VkBuffer dstBuffer,
            uint32_t regionCount,
            const VkBufferCopy *pRegions);

    } else if (d == 2) {
    } else {
        for (int i = 0; i < (int)c.extent[d - 1]; i++) {
            int err = do_multidimensional_copy(user_context, ctx, c, off, d - 1, d_to_h);
            off += c.src_stride_bytes[d - 1];
            if (err) {
                return err;
            }
        }
    }
    return 0;
}
}  // namespace

WEAK int halide_vulkan_copy_to_device(void *user_context, halide_buffer_t *halide_buffer) {
    int err = halide_vulkan_device_malloc(user_context, halide_buffer);
    if (err) {
        return err;
    }

    debug(user_context)
        << "Vulkan: halide_vulkan_copy_to_device (user_context: " << user_context
        << ", halide_buffer: " << halide_buffer << ")\n";

    // Acquire the context so we can use the command queue.
    VulkanContext ctx(user_context);
    if (ctx.error != VK_SUCCESS) {
        return ctx.error;
    }

#ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
#endif

    halide_abort_if_false(user_context, halide_buffer->host && halide_buffer->device);

    device_copy copy_helper = make_host_to_device_copy(halide_buffer);

    // We construct a staging buffer to copy into from host memory.  Then,
    // we use vkCmdCopyBuffer() to copy from the staging buffer into the
    // the actual device memory.
    MemoryRequest request = {0};
    request.size = halide_buffer->size_in_bytes();
    request.properties.usage = MemoryUsage::TransferSrc;
    request.properties.caching = MemoryCaching::UncachedCoherent;
    request.properties.visibility = MemoryVisibility::HostToDevice;

    // allocate a new region
    memory_allocator->bind(user_context, ctx.device, ctx.physical_device);
    MemoryRegion *region = memory_allocator->reserve(user_context, request);
    if ((region == nullptr) || (region->handle == nullptr)) {
        error(user_context) << "Vulkan: Failed to allocate device memory!\n";
        return -1;
    }

    // retrieve the buffer from the region
    VkBuffer staging_buffer = reinterpret_cast<VkBuffer>(region->handle);

    // map the region to a host ptr
    uint8_t *stage_host_ptr = (uint8_t *)memory_allocator->map(user_context, region);
    if (stage_host_ptr == nullptr) {
        error(user_context) << "Vulkan: Failed to map host pointer to device memory!\n";
        return halide_error_code_internal_error;
    }

    // copy to the (host-visible/coherent) staging buffer
    copy_helper.dst = (uint64_t)(stage_host_ptr);
    copy_memory(copy_helper, user_context);

    // unmap the pointer
    memory_allocator->unmap(user_context, region);
    memory_allocator->unbind(user_context);

    // TODO: only copy the regions that should be copied
    VkBufferCopy staging_copy = {
        0,                              // srcOffset
        0,                              // dstOffset
        halide_buffer->size_in_bytes()  // size
    };

    // create a command buffer
    VkCommandPool command_pool;
    VkCommandBuffer command_buffer;
    VkResult result = vk_create_command_pool(ctx.device, ctx.queue_family_index, ctx.allocation_callbacks(), &command_pool);
    if (result != VK_SUCCESS) {
        debug(user_context) << "Vulkan: vkCreateCommandPool returned: " << get_vulkan_error_name(result) << "\n";
        return result;
    }

    result = vk_create_command_buffer(ctx.device, command_pool, &command_buffer);
    if (result != VK_SUCCESS) {
        debug(user_context) << "Vulkan: vkCreateCommandBuffer returned: " << get_vulkan_error_name(result) << "\n";
        return result;
    }

    // begin the command buffer
    VkCommandBufferBeginInfo command_buffer_begin_info =
        {
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,  // struct type
            nullptr,                                      // pointer to struct extending this
            VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,  // flags
            nullptr                                       // pointer to parent command buffer
        };

    result = vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info);
    if (result != VK_SUCCESS) {
        debug(user_context) << "vkBeginCommandBuffer returned " << get_vulkan_error_name(result) << "\n";
        return result;
    }

    // enqueue the copy operation
    vkCmdCopyBuffer(command_buffer, staging_buffer, (VkBuffer)(halide_buffer->device), 1, &staging_copy);

    // end the command buffer
    result = vkEndCommandBuffer(command_buffer);
    if (result != VK_SUCCESS) {
        debug(user_context) << "vkEndCommandBuffer returned " << get_vulkan_error_name(result) << "\n";
        return result;
    }

    //// 13. Submit the command buffer to our command queue
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

    result = vkQueueSubmit(ctx.queue, 1, &submit_info, 0);
    if (result != VK_SUCCESS) {
        debug(user_context) << "vkQueueSubmit returned " << get_vulkan_error_name(result) << "\n";
        return result;
    }

    //// 14. Wait until the queue is done with the command buffer
    result = vkQueueWaitIdle(ctx.queue);
    if (result != VK_SUCCESS) {
        debug(user_context) << "vkQueueWaitIdle returned " << get_vulkan_error_name(result) << "\n";
        return result;
    }

    //// 15. Reclaim the staging buffer
    memory_allocator->bind(user_context, ctx.device, ctx.physical_device);
    memory_allocator->reclaim(user_context, region);
    memory_allocator->unbind(user_context);

    //do_multidimensional_copy(user_context, ctx, c, 0, buf->dimensions, false);

#ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
#endif

    return 0;
}

WEAK int halide_vulkan_copy_to_host(void *user_context, halide_buffer_t *halide_buffer) {
    debug(user_context)
        << "Vulkan: halide_copy_to_host (user_context: " << user_context
        << ", halide_buffer: " << halide_buffer << ")\n";

    // Acquire the context so we can use the command queue. This also avoids multiple
    // redundant calls to clEnqueueReadBuffer when multiple threads are trying to copy
    // the same buffer.
    VulkanContext ctx(user_context);
    if (ctx.error != VK_SUCCESS) {
        return ctx.error;
    }

#ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
#endif

    halide_abort_if_false(user_context, halide_buffer->host && halide_buffer->device);

    device_copy copy_helper = make_device_to_host_copy(halide_buffer);

    //do_multidimensional_copy(user_context, ctx, c, 0, buf->dimensions, true);

    // This is the inverse of copy_to_device: we create a staging buffer, copy into
    // it, map it so the host can see it, then copy into the host buffer

    MemoryRequest request = {0};
    request.size = halide_buffer->size_in_bytes();
    request.properties.usage = MemoryUsage::TransferDst;
    request.properties.caching = MemoryCaching::UncachedCoherent;
    request.properties.visibility = MemoryVisibility::HostToDevice;

    // allocate a new region
    memory_allocator->bind(user_context, ctx.device, ctx.physical_device);
    MemoryRegion *region = memory_allocator->reserve(user_context, request);
    if ((region == nullptr) || (region->handle == nullptr)) {
        error(user_context) << "Vulkan: Failed to allocate device memory!\n";
        return -1;
    }

    // retrieve the buffer from the region
    VkBuffer staging_buffer = reinterpret_cast<VkBuffer>(region->handle);

    // TODO: only copy the regions that should be copied
    VkBufferCopy staging_copy = {
        0,                              // srcOffset
        0,                              // dstOffset
        halide_buffer->size_in_bytes()  // size
    };

    // create a command buffer
    VkCommandPool command_pool;
    VkCommandBuffer command_buffer;
    VkResult result = vk_create_command_pool(ctx.device, ctx.queue_family_index, ctx.allocation_callbacks(), &command_pool);
    if (result != VK_SUCCESS) {
        debug(user_context) << "Vulkan: vkCreateCommandPool returned: " << get_vulkan_error_name(result) << "\n";
        return -1;
    }

    result = vk_create_command_buffer(ctx.device, command_pool, &command_buffer);

    // begin the command buffer
    VkCommandBufferBeginInfo command_buffer_begin_info =
        {
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,  // struct type
            nullptr,                                      // pointer to struct extending this
            VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,  // flags
            nullptr                                       // pointer to parent command buffer
        };

    result = vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info);
    if (result != VK_SUCCESS) {
        debug(user_context) << "vkBeginCommandBuffer returned " << get_vulkan_error_name(result) << "\n";
        return result;
    }

    // enqueue the copy operation
    vkCmdCopyBuffer(command_buffer, (VkBuffer)(halide_buffer->device), staging_buffer, 1, &staging_copy);

    // end the command buffer
    result = vkEndCommandBuffer(command_buffer);
    if (result != VK_SUCCESS) {
        debug(user_context) << "vkEndCommandBuffer returned " << get_vulkan_error_name(result) << "\n";
        return result;
    }

    //// 13. Submit the command buffer to our command queue
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

    result = vkQueueSubmit(ctx.queue, 1, &submit_info, 0);
    if (result != VK_SUCCESS) {
        debug(user_context) << "vkQueueSubmit returned " << get_vulkan_error_name(result) << "\n";
        return result;
    }

    //// 14. Wait until the queue is done with the command buffer
    result = vkQueueWaitIdle(ctx.queue);
    if (result != VK_SUCCESS) {
        debug(user_context) << "vkQueueWaitIdle returned " << get_vulkan_error_name(result) << "\n";
        return result;
    }

    // map the region to a host ptr
    uint8_t *stage_host_ptr = (uint8_t *)memory_allocator->map(user_context, region);
    if (stage_host_ptr == nullptr) {
        error(user_context) << "Vulkan: Failed to map host pointer to device memory!\n";
        return halide_error_code_internal_error;
    }

    // copy to the (host-visible/coherent) staging buffer
    copy_helper.src = (uint64_t)(stage_host_ptr);
    copy_memory(copy_helper, user_context);

    // unmap the pointer
    memory_allocator->unmap(user_context, region);
    memory_allocator->reclaim(user_context, region);
    memory_allocator->unbind(user_context);

#ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
#endif

    return 0;
}

WEAK int halide_vulkan_run(void *user_context,
                           void *state_ptr,
                           const char *entry_name,
                           int blocksX, int blocksY, int blocksZ,
                           int threadsX, int threadsY, int threadsZ,
                           int shared_mem_bytes,
                           size_t arg_sizes[],
                           void *args[],
                           int8_t arg_is_buffer[]) {
    debug(user_context)
        << "Vulkan: halide_vulkan_run (user_context: " << user_context << ", "
        << "entry: " << entry_name << ", "
        << "blocks: " << blocksX << "x" << blocksY << "x" << blocksZ << ", "
        << "threads: " << threadsX << "x" << threadsY << "x" << threadsZ << ", "
        << "shmem: " << shared_mem_bytes << "\n";

    VulkanContext ctx(user_context);
    if (ctx.error != VK_SUCCESS) {
        return ctx.error;
    }

#ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
#endif

    // Running a Vulkan pipeline requires a large number of steps
    // and boilerplate:
    // 1. Create a descriptor set layout
    // 1a. Create the buffer for the scalar params
    // 2. Create a pipeline layout
    // 3. Create a compute pipeline
    // --- The above can be cached between invocations ---
    // 4. Create a descriptor set
    // 5. Set bindings for buffers in the descriptor set
    // 6. Create a command pool
    // 7. Create a command buffer from the command pool
    // 8. Begin the command buffer
    // 9. Bind the compute pipeline from #3
    // 10. Bind the descriptor set
    // 11. Add a dispatch to the command buffer
    // 12. End the command buffer
    // 13. Submit the command buffer to our command queue
    // --- The following isn't best practice, but it's in line
    //     with what we do in Metal etc. ---
    // 14. Wait until the queue is done with the command buffer

    //// 1. Create a descriptor set layout
    const size_t HALIDE_MAX_VK_BINDINGS = 64;
    VkDescriptorSetLayoutBinding layout_bindings[HALIDE_MAX_VK_BINDINGS];

    // The first binding is used for scalar parameters
    uint32_t num_bindings = 1;
    layout_bindings[0] = {0,                                  // binding
                          VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,  // descriptor type
                          1,                                  // descriptor count
                          VK_SHADER_STAGE_COMPUTE_BIT,        // stage flags
                          0};                                 // immutable samplers

    int i = 0;
    int scalar_buffer_size = 0;
    while (arg_sizes[i] > 0) {
        if (arg_is_buffer[i]) {
            // TODO: I don't quite understand why STORAGE_BUFFER is valid
            // here, but examples all across the docs seem to do this
            layout_bindings[num_bindings] =
                {num_bindings,                       // binding
                 VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,  // descriptor type
                 1,                                  // descriptor count
                 VK_SHADER_STAGE_COMPUTE_BIT,        // stage flags
                 nullptr};                           // immutable samplers
            num_bindings++;
        } else {
            scalar_buffer_size += arg_sizes[i];
        }
        i++;
    }
    // Create the LayoutInfo struct
    VkDescriptorSetLayoutCreateInfo layout_info =
        {
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,  // structure type
            nullptr,                                              // pointer to a struct extending this info
            0,                                                    // flags
            num_bindings,                                         // binding count
            layout_bindings                                       // pointer to layout bindings array
        };

    // Create the descriptor set layout
    VkDescriptorSetLayout descriptor_set_layout;
    auto result = vkCreateDescriptorSetLayout(ctx.device, &layout_info, 0, &descriptor_set_layout);

    if (result != VK_SUCCESS) {
        debug(user_context) << "vkCreateDescriptorSetLayout returned " << get_vulkan_error_name(result) << "\n";
        return result;
    }

    //// 1a. Create a buffer for the scalar parameters
    // First allocate memory, then map it and copy params, then create a buffer
    // and bind the allocation
    // VkMemoryAllocateInfo scalar_alloc_info =
    //     {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,    // struct type
    //      nullptr,   // point to struct extending this
    //      (uint32_t)scalar_buffer_size,    // allocation size
    //      ctx.memory_type_index  // memory type
    //     };
    VkDeviceMemory scalar_alloc;
    //result = vkAllocateMemory(ctx.device, &scalar_alloc_info, 0, &scalar_alloc);
    result = vk_allocate_device_memory(ctx.physical_device, ctx.device, scalar_buffer_size,
                                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                       ctx.allocation_callbacks(), &scalar_alloc);
    if (result != VK_SUCCESS) {
        debug(user_context) << "Vulkan: vk_allocate_device_memory() failed "
                            << "\n";
        return result;
    }

    uint8_t *scalar_ptr;
    result = vkMapMemory(ctx.device, scalar_alloc, 0, scalar_buffer_size, 0, (void **)&scalar_ptr);

    if (result != VK_SUCCESS) {
        debug(user_context) << "vkMapMemory returned " << get_vulkan_error_name(result) << "\n";
        return result;
    }

    size_t scalar_arg_offset = 0;
    debug(user_context) << "Parameter: (passed in vs value after copy)"
                        << "\n";
    for (size_t i = 0; arg_sizes[i] > 0; i++) {
        if (!arg_is_buffer[i]) {
            //int one = 1;
            memcpy(scalar_ptr + scalar_arg_offset, args[i], arg_sizes[i]);
            debug(user_context) << *((int32_t *)(scalar_ptr + scalar_arg_offset));
            debug(user_context) << "   " << *((int32_t *)(args[i])) << "\n";
            scalar_arg_offset += arg_sizes[i];
        }
    }
    vkUnmapMemory(ctx.device, scalar_alloc);

    VkBufferCreateInfo scalar_buffer_info =
        {
            VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,  // struct type
            nullptr,                               // point to struct extending this
            0,                                     // flags
            (VkDeviceSize)scalar_buffer_size,      // size
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,    // usages
            VK_SHARING_MODE_EXCLUSIVE,             // sharing across queues
            0,                                     // irrelevant here
            nullptr                                // irrelevant
        };

    VkBuffer scalar_args_buffer;
    result = vkCreateBuffer(ctx.device, &scalar_buffer_info, ctx.allocation_callbacks(), &scalar_args_buffer);

    if (result != VK_SUCCESS) {
        debug(user_context) << "vkCreateBuffer returned " << get_vulkan_error_name(result) << "\n";
        return result;
    }

    result = vkBindBufferMemory(ctx.device, scalar_args_buffer, scalar_alloc, 0);

    if (result != VK_SUCCESS) {
        debug(user_context) << "vkBindBufferMemory returned " << get_vulkan_error_name(result) << "\n";
        return result;
    }

    ///// 2. Create a pipeline layout
    VkPipelineLayoutCreateInfo pipeline_layout_info =
        {
            VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,  // structure type
            nullptr,                                        // pointer to a structure extending this
            0,                                              // flags
            1,                                              // number of descriptor sets
            &descriptor_set_layout,                         // pointer to the descriptor sets
            0,                                              // number of push constant ranges
            nullptr                                         // pointer to push constant range structs
        };

    VkPipelineLayout pipeline_layout;
    result = vkCreatePipelineLayout(ctx.device, &pipeline_layout_info, 0, &pipeline_layout);

    if (result != VK_SUCCESS) {
        debug(user_context) << "vkCreatePipelineLayout returned " << get_vulkan_error_name(result) << "\n";
        return result;
    }

    //// 3. Create a compute pipeline
    // Get the shader module

    VkShaderModule* shader_module = nullptr;
    bool found = compilation_cache.lookup(ctx.device, state_ptr, shader_module);
    halide_abort_if_false(user_context, found);
    halide_abort_if_false(user_context, shader_module != nullptr);
    VkComputePipelineCreateInfo compute_pipeline_info =
        {
            VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,  // structure type
            nullptr,                                         // pointer to a structure extending this
            0,                                               // flags
            // VkPipelineShaderStageCreatInfo
            {
                VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,  // structure type
                nullptr,                                              //pointer to a structure extending this
                0,                                                    // flags
                VK_SHADER_STAGE_COMPUTE_BIT,                          // compute stage shader
                *shader_module,                                       // shader module
                entry_name,                                           // entry point name
                nullptr                                               // pointer to VkSpecializationInfo struct
            },
            pipeline_layout,  // pipeline layout
            0,                // base pipeline handle for derived pipeline
            0                 // base pipeline index for derived pipeline
        };

    VkPipeline pipeline;
    result = vkCreateComputePipelines(ctx.device, 0, 1, &compute_pipeline_info, 0, &pipeline);

    if (result != VK_SUCCESS) {
        debug(user_context) << "vkCreateComputePipelines returned " << get_vulkan_error_name(result) << "\n";
        return result;
    }

    //// 4. Create a descriptor set
    VkDescriptorPoolSize descriptor_pool_sizes[2] = {
        {
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,  // descriptor type
            1                                   // how many
        },
        {
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,  // descriptor type
            num_bindings - 1                    // how many
        }};

    VkDescriptorPoolCreateInfo descriptor_pool_info =
        {
            VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,  // struct type
            nullptr,                                        // point to struct extending this
            0,                                              // flags
            num_bindings,                                   // max numbewr of sets that can be allocated TODO:should this be 1?
            2,                                              // pool size count
            descriptor_pool_sizes                           // ptr to descriptr pool sizes
        };

    VkDescriptorPool descriptor_pool;
    result = vkCreateDescriptorPool(ctx.device, &descriptor_pool_info, 0, &descriptor_pool);

    if (result != VK_SUCCESS) {
        debug(user_context) << "vkCreateDescriptorPool returned " << get_vulkan_error_name(result) << "\n";
        return result;
    }

    VkDescriptorSetAllocateInfo descriptor_set_info =
        {
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,  // struct type
            nullptr,                                         // pointer to struct extending this
            descriptor_pool,                                 // pool from which to allocate sets
            1,                                               // number of descriptor sets
            &descriptor_set_layout                           // pointer to array of descriptor set layouts
        };

    VkDescriptorSet descriptor_set;
    result = vkAllocateDescriptorSets(ctx.device, &descriptor_set_info, &descriptor_set);

    if (result != VK_SUCCESS) {
        debug(user_context) << "vkAllocateDescriptorSets returned " << get_vulkan_error_name(result) << "\n";
        return result;
    }

    //// 5. Set bindings for buffers in the descriptor set
    VkDescriptorBufferInfo descriptor_buffer_info[HALIDE_MAX_VK_BINDINGS];
    VkWriteDescriptorSet write_descriptor_set[HALIDE_MAX_VK_BINDINGS];

    // First binding will be the scalar params buffer
    descriptor_buffer_info[0] =
        {
            scalar_args_buffer,  // the buffer
            0,                   // offset
            VK_WHOLE_SIZE        // range
        };
    write_descriptor_set[0] =
        {
            VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,  // struct type
            nullptr,                                 // pointer to struct extending this
            descriptor_set,                          // descriptor set to update
            0,                                       // binding
            0,                                       // array elem
            1,                                       // num to update
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,       // descriptor type
            nullptr,                                 // for images
            &(descriptor_buffer_info[0]),            // info for buffer
            nullptr                                  // for texel buffers
        };
    uint32_t num_bound = 1;
    for (size_t i = 0; arg_sizes[i] > 0; i++) {
        if (arg_is_buffer[i]) {
            halide_debug_assert(user_context, num_bound < HALIDE_MAX_VK_BINDINGS);
            auto buf = (VkBuffer)(((halide_buffer_t *)args[i])->device);
            descriptor_buffer_info[num_bound] =
                {
                    buf,           // the buffer
                    0,             // offset
                    VK_WHOLE_SIZE  // range
                };
            write_descriptor_set[num_bound] =
                {
                    VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,  // struct type
                    nullptr,                                 // pointer to struct extending this
                    descriptor_set,                          // descriptor set to update
                    num_bound,                               // binding
                    0,                                       // array elem
                    1,                                       // num to update
                    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,       // descriptor type
                    nullptr,                                 // for images
                    &(descriptor_buffer_info[num_bound]),    // info for buffer
                    nullptr                                  // for texel buffers
                };
            num_bound++;
        }
    }

    halide_debug_assert(user_context, num_bound == num_bindings);
    vkUpdateDescriptorSets(ctx.device, num_bindings, write_descriptor_set, 0, nullptr);

    //// 6. Create a command pool
    // TODO: This should really be part of the acquire_context API
    // VkCommandPoolCreateInfo command_pool_info =
    //     {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,    // struct type
    //      nullptr,   // pointer to struct extending this
    //      0,     // flags.  may consider VK_COMMAND_POOL_CREATE_TRANSIENT_BIT
    //      ctx.queue_family_index     // queue family index corresponding to the compute command queue
    //     };
    VkCommandPool command_pool;
    // result = vkCreateCommandPool(ctx.device, &command_pool_info, ctx.allocation_callbacks(), &command_pool);
    result = vk_create_command_pool(ctx.device, ctx.queue_family_index, ctx.allocation_callbacks(), &command_pool);

    if (result != VK_SUCCESS) {
        debug(user_context) << "vkCreateCommandPool returned " << get_vulkan_error_name(result) << "\n";
        return result;
    }

    //// 7. Create a command buffer from the command pool
    // VkCommandBufferAllocateInfo command_buffer_info =
    //     {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,    // struct type
    //      nullptr,    // pointer to struct extending this
    //      command_pool,  // command pool for allocation
    //      VK_COMMAND_BUFFER_LEVEL_PRIMARY,   // command buffer level
    //      1  // number to allocate
    //     };

    VkCommandBuffer command_buffer;
    //result = vkAllocateCommandBuffers(ctx.device, &command_buffer_info, &command_buffer);
    result = vk_create_command_buffer(ctx.device, command_pool, &command_buffer);

    if (result != VK_SUCCESS) {
        debug(user_context) << "vkAllocateCommandBuffer returned " << get_vulkan_error_name(result) << "\n";
        return result;
    }

    //// 8. Begin the command buffer
    VkCommandBufferBeginInfo command_buffer_begin_info =
        {
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,  // struct type
            nullptr,                                      // pointer to struct extending this
            VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,  // flags
            nullptr                                       // pointer to parent command buffer
        };

    result = vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info);

    if (result != VK_SUCCESS) {
        debug(user_context) << "vkBeginCommandBuffer returned " << get_vulkan_error_name(result) << "\n";
        return result;
    }

    //// 9. Bind the compute pipeline from #3
    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

    //// 10. Bind the descriptor set
    vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout,
                            0, 1, &descriptor_set, 0, nullptr);

    //// 11. Add a dispatch to the command buffer
    // TODO: Is this right?
    vkCmdDispatch(command_buffer, blocksX, blocksY, blocksZ);

    //// 12. End the command buffer
    result = vkEndCommandBuffer(command_buffer);

    if (result != VK_SUCCESS) {
        debug(user_context) << "vkEndCommandBuffer returned " << get_vulkan_error_name(result) << "\n";
        return result;
    }

    //// 13. Submit the command buffer to our command queue
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

    result = vkQueueSubmit(ctx.queue, 1, &submit_info, 0);

    if (result != VK_SUCCESS) {
        debug(user_context) << "vkQueueSubmit returned " << get_vulkan_error_name(result) << "\n";
        return result;
    }

    //// 14. Wait until the queue is done with the command buffer
    result = vkQueueWaitIdle(ctx.queue);

    if (result != VK_SUCCESS) {
        debug(user_context) << "vkQueueWaitIdle returned " << get_vulkan_error_name(result) << "\n";
        return result;
    }

#ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
#endif
    return 0;
}

WEAK int halide_vulkan_device_and_host_malloc(void *user_context, struct halide_buffer_t *buf) {
    return halide_default_device_and_host_malloc(user_context, buf, &vulkan_device_interface);
}

WEAK int halide_vulkan_device_and_host_free(void *user_context, struct halide_buffer_t *buf) {
    return halide_default_device_and_host_free(user_context, buf, &vulkan_device_interface);
}

WEAK int halide_vulkan_wrap_vk_buffer(void *user_context, struct halide_buffer_t *buf, uint64_t vk_buffer) {
    halide_debug_assert(user_context, buf->device == 0);
    if (buf->device != 0) {
        return -2;
    }
    buf->device = vk_buffer;
    buf->device_interface = &vulkan_device_interface;
    buf->device_interface->impl->use_module();

    return 0;
}

WEAK int halide_vulkan_detach_vk_buffer(void *user_context, halide_buffer_t *buf) {
    if (buf->device == 0) {
        return 0;
    }
    halide_debug_assert(user_context, buf->device_interface == &vulkan_device_interface);
    buf->device = 0;
    buf->device_interface->impl->release_module();
    buf->device_interface = nullptr;
    return 0;
}

WEAK uintptr_t halide_vulkan_get_vk_buffer(void *user_context, halide_buffer_t *buf) {
    if (buf->device == 0) {
        return 0;
    }
    halide_debug_assert(user_context, buf->device_interface == &vulkan_device_interface);
    return (uintptr_t)buf->device;
}

WEAK const struct halide_device_interface_t *halide_vulkan_device_interface() {
    return &vulkan_device_interface;
}

namespace {
__attribute__((destructor))
WEAK void
halide_vulkan_cleanup() {
    halide_vulkan_device_release(nullptr);
}
}  // namespace

}  // extern "C" linkage

namespace Halide {
namespace Runtime {
namespace Internal {
namespace Vulkan {

WEAK halide_device_interface_impl_t vulkan_device_interface_impl = {
    halide_use_jit_module,
    halide_release_jit_module,
    halide_vulkan_device_malloc,
    halide_vulkan_device_free,
    halide_vulkan_device_sync,
    halide_vulkan_device_release,
    halide_vulkan_copy_to_host,
    halide_vulkan_copy_to_device,
    halide_vulkan_device_and_host_malloc,
    halide_vulkan_device_and_host_free,
    halide_default_buffer_copy,
    halide_default_device_crop,
    halide_default_device_slice,
    halide_default_device_release_crop,
    halide_vulkan_wrap_vk_buffer,
    halide_vulkan_detach_vk_buffer,
};

WEAK halide_device_interface_t vulkan_device_interface = {
    halide_device_malloc,
    halide_device_free,
    halide_device_sync,
    halide_device_release,
    halide_copy_to_host,
    halide_copy_to_device,
    halide_device_and_host_malloc,
    halide_device_and_host_free,
    halide_buffer_copy,
    halide_device_crop,
    halide_device_slice,
    halide_device_release_crop,
    halide_device_wrap_native,
    halide_device_detach_native,
    nullptr,  // target capabilities.
    &vulkan_device_interface_impl};

}  // namespace Vulkan
}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide
