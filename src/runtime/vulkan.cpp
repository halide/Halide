#include "HalideRuntimeVulkan.h"

#include "device_buffer_utils.h"
#include "device_interface.h"
#include "runtime_internal.h"
#include "vulkan_context.h"
#include "vulkan_extensions.h"
#include "vulkan_internal.h"
#include "vulkan_memory.h"
#include "vulkan_resources.h"

using namespace Halide::Runtime::Internal::Vulkan;

// --------------------------------------------------------------------------

extern "C" {

// --------------------------------------------------------------------------

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
                                       halide_vulkan_memory_allocator **allocator,
                                       VkInstance *instance,
                                       VkDevice *device,
                                       VkPhysicalDevice *physical_device,
                                       VkCommandPool *command_pool,
                                       VkQueue *queue,
                                       uint32_t *queue_family_index,
                                       bool create) {

    halide_debug_assert(user_context, instance != nullptr);
    halide_debug_assert(user_context, device != nullptr);
    halide_debug_assert(user_context, queue != nullptr);
    halide_debug_assert(user_context, &thread_lock != nullptr);
    while (__atomic_test_and_set(&thread_lock, __ATOMIC_ACQUIRE)) {}

    // If the context has not been initialized, initialize it now.
    halide_debug_assert(user_context, &cached_instance != nullptr);
    halide_debug_assert(user_context, &cached_device != nullptr);
    halide_debug_assert(user_context, &cached_queue != nullptr);
    halide_debug_assert(user_context, &cached_physical_device != nullptr);
    if ((cached_instance == nullptr) && create) {
        int result = vk_create_context(user_context,
                                       reinterpret_cast<VulkanMemoryAllocator **>(&cached_allocator),
                                       &cached_instance,
                                       &cached_device,
                                       &cached_physical_device,
                                       &cached_command_pool,
                                       &cached_queue,
                                       &cached_queue_family_index);
        if (result != halide_error_code_success) {
            __atomic_clear(&thread_lock, __ATOMIC_RELEASE);
            return result;
        }
    }

    *allocator = cached_allocator;
    *instance = cached_instance;
    *device = cached_device;
    *physical_device = cached_physical_device;
    *command_pool = cached_command_pool;
    *queue = cached_queue;
    *queue_family_index = cached_queue_family_index;
    return 0;
}

WEAK int halide_vulkan_release_context(void *user_context, VkInstance instance, VkDevice device, VkQueue queue) {
    __atomic_clear(&thread_lock, __ATOMIC_RELEASE);
    return 0;
}

WEAK int halide_vulkan_device_free(void *user_context, halide_buffer_t *halide_buffer) {
    debug(user_context)
        << "halide_vulkan_device_free (user_context: " << user_context
        << ", halide_buffer: " << halide_buffer << ")\n";

    // halide_vulkan_device_free, at present, can be exposed to clients and they
    // should be allowed to call halide_vulkan_device_free on any halide_buffer_t
    // including ones that have never been used with a GPU.
    if (halide_buffer->device == 0) {
        return 0;
    }

    VulkanContext ctx(user_context);

#ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
#endif

    // get the allocated region for the device
    MemoryRegion *device_region = reinterpret_cast<MemoryRegion *>(halide_buffer->device);
    MemoryRegion *memory_region = ctx.allocator->owner_of(user_context, device_region);
    if (ctx.allocator && memory_region && memory_region->handle) {
        if (halide_can_reuse_device_allocations(user_context)) {
            ctx.allocator->release(user_context, memory_region);
        } else {
            ctx.allocator->reclaim(user_context, memory_region);
        }
    }
    halide_buffer->device = 0;
    halide_buffer->device_interface->impl->release_module();
    halide_buffer->device_interface = nullptr;

#ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
#endif

    return 0;
}

WEAK int halide_vulkan_initialize_kernels(void *user_context, void **state_ptr, const char *src, int size) {
    debug(user_context)
        << "halide_vulkan_init_kernels (user_context: " << user_context
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
    VulkanCompilationCacheEntry *cache_entry = nullptr;
    if (!compilation_cache.kernel_state_setup(user_context, state_ptr, ctx.device, cache_entry,
                                              Halide::Runtime::Internal::Vulkan::vk_compile_shader_module,
                                              user_context, ctx.allocator, src, size)) {
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
        << "halide_vulkan_finalize_kernels (user_context: " << user_context
        << ", state_ptr: " << state_ptr << "\n";

#ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
#endif

    VulkanContext ctx(user_context);
    if (ctx.error == VK_SUCCESS) {
        compilation_cache.release_hold(user_context, ctx.device, state_ptr);
    }

#ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
#endif
}

// Used to generate correct timings when tracing
WEAK int halide_vulkan_device_sync(void *user_context, halide_buffer_t *) {
    debug(user_context) << "halide_vulkan_device_sync (user_context: " << user_context << ")\n";

    VulkanContext ctx(user_context);
    halide_debug_assert(user_context, ctx.error == VK_SUCCESS);

#ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
#endif

    vkQueueWaitIdle(ctx.queue);

#ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
#endif

    return VK_SUCCESS;
}

WEAK int halide_vulkan_device_release(void *user_context) {
    debug(user_context)
        << "halide_vulkan_device_release (user_context: " << user_context << ")\n";

    VulkanMemoryAllocator *allocator;
    VkInstance instance;
    VkDevice device;
    VkCommandPool command_pool;
    VkPhysicalDevice physical_device;
    VkQueue queue;
    uint32_t _throwaway;

    int acquire_status = halide_vulkan_acquire_context(user_context,
                                                       reinterpret_cast<halide_vulkan_memory_allocator **>(&allocator),
                                                       &instance, &device, &physical_device, &command_pool, &queue, &_throwaway, false);
    halide_debug_assert(user_context, acquire_status == VK_SUCCESS);
    (void)acquire_status;
    if (instance != nullptr) {

        vkQueueWaitIdle(queue);
        if (command_pool == cached_command_pool) {
            cached_command_pool = 0;
        }
        if (reinterpret_cast<halide_vulkan_memory_allocator *>(allocator) == cached_allocator) {
            cached_allocator = nullptr;
        }

        vk_destroy_command_pool(user_context, allocator, command_pool);
        vk_destroy_shader_modules(user_context, allocator);
        vk_destroy_memory_allocator(user_context, allocator);

        if (device == cached_device) {
            cached_device = nullptr;
            cached_physical_device = nullptr;
            cached_queue = nullptr;
            cached_queue_family_index = 0;
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

WEAK int halide_vulkan_device_malloc(void *user_context, halide_buffer_t *buf) {
    debug(user_context)
        << "halide_vulkan_device_malloc (user_context: " << user_context
        << ", buf: " << buf << ")\n";

    VulkanContext ctx(user_context);
    if (ctx.error != VK_SUCCESS) {
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

    debug(user_context) << "    allocating buffer: "
                        << "extents: " << buf->dim[0].extent << "x"
                        << buf->dim[1].extent << "x" << buf->dim[2].extent << "x"
                        << buf->dim[3].extent << " "
                        << "strides: " << buf->dim[0].stride << "x"
                        << buf->dim[1].stride << "x" << buf->dim[2].stride << "x"
                        << buf->dim[3].stride << " "
                        << "type: " << buf->type << " "
                        << "size_in_bytes: " << (uint64_t)size << " "
                        << "(or " << (size * 1e-6f) << "MB)\n";

#ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
#endif

    // request uncached device only memory
    MemoryRequest request = {0};
    request.size = size;
    request.properties.usage = MemoryUsage::TransferSrcDst;
    request.properties.caching = MemoryCaching::Uncached;
    request.properties.visibility = MemoryVisibility::DeviceOnly;

    // allocate a new region
    MemoryRegion *device_region = ctx.allocator->reserve(user_context, request);
    if ((device_region == nullptr) || (device_region->handle == nullptr)) {
        error(user_context) << "Vulkan: Failed to allocate device memory!\n";
        return -1;
    }

    buf->device = (uint64_t)device_region;
    buf->device_interface = &vulkan_device_interface;
    buf->device_interface->impl->use_module();

    debug(user_context)
        << "    Allocated device buffer " << (void *)buf->device
        << " for buffer " << buf << "\n";

    // retrieve the buffer from the region
    VkBuffer *device_buffer = reinterpret_cast<VkBuffer *>(device_region->handle);
    if (device_buffer == nullptr) {
        error(user_context) << "Vulkan: Failed to retrieve device buffer for device memory!\n";
        return halide_error_code_internal_error;
    }

    // create a command buffer
    VkCommandBuffer command_buffer;
    VkResult result = vk_create_command_buffer(user_context, ctx.allocator, ctx.command_pool, &command_buffer);
    if (result != VK_SUCCESS) {
        debug(user_context) << "Vulkan: vkCreateCommandBuffer returned: " << vk_get_error_name(result) << "\n";
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
        debug(user_context) << "vkBeginCommandBuffer returned " << vk_get_error_name(result) << "\n";
        return result;
    }

    // fill buffer with zero values
    vkCmdFillBuffer(command_buffer, *device_buffer, 0, device_region->size, 0);
    debug(user_context) << "    zeroing device_buffer=" << (void *)device_buffer
                        << " size=" << (uint32_t)device_region->size << "\n";

    // end the command buffer
    result = vkEndCommandBuffer(command_buffer);
    if (result != VK_SUCCESS) {
        debug(user_context) << "vkEndCommandBuffer returned " << vk_get_error_name(result) << "\n";
        return result;
    }

    // submit the command buffer
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
        debug(user_context) << "vkQueueSubmit returned " << vk_get_error_name(result) << "\n";
        return result;
    }

    // wait for memset to finish
    result = vkQueueWaitIdle(ctx.queue);
    if (result != VK_SUCCESS) {
        debug(user_context) << "vkQueueWaitIdle returned " << vk_get_error_name(result) << "\n";
        return result;
    }

    vk_destroy_command_buffer(user_context, ctx.allocator, ctx.command_pool, command_buffer);

#ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
#endif

    return 0;
}

WEAK int halide_vulkan_copy_to_device(void *user_context, halide_buffer_t *halide_buffer) {
    int err = halide_vulkan_device_malloc(user_context, halide_buffer);
    if (err) {
        return err;
    }

    debug(user_context)
        << "halide_vulkan_copy_to_device (user_context: " << user_context
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
    MemoryRegion *staging_region = ctx.allocator->reserve(user_context, request);
    if ((staging_region == nullptr) || (staging_region->handle == nullptr)) {
        error(user_context) << "Vulkan: Failed to allocate device memory!\n";
        return -1;
    }

    // map the region to a host ptr
    uint8_t *stage_host_ptr = (uint8_t *)ctx.allocator->map(user_context, staging_region);
    if (stage_host_ptr == nullptr) {
        error(user_context) << "Vulkan: Failed to map host pointer to device memory!\n";
        return halide_error_code_internal_error;
    }

    // copy to the (host-visible/coherent) staging buffer
    copy_helper.dst = (uint64_t)(stage_host_ptr);
    copy_memory(copy_helper, user_context);

    // retrieve the buffer from the region
    VkBuffer *staging_buffer = reinterpret_cast<VkBuffer *>(staging_region->handle);
    if (staging_buffer == nullptr) {
        error(user_context) << "Vulkan: Failed to retrieve staging buffer for device memory!\n";
        return halide_error_code_internal_error;
    }

    // unmap the pointer
    ctx.allocator->unmap(user_context, staging_region);

    // get the allocated region for the device
    MemoryRegion *device_region = reinterpret_cast<MemoryRegion *>(halide_buffer->device);
    MemoryRegion *memory_region = ctx.allocator->owner_of(user_context, device_region);

    // retrieve the buffer from the region
    VkBuffer *device_buffer = reinterpret_cast<VkBuffer *>(memory_region->handle);
    if (device_buffer == nullptr) {
        error(user_context) << "Vulkan: Failed to retrieve buffer for device memory!\n";
        return halide_error_code_internal_error;
    }

    // create a command buffer
    VkCommandBuffer command_buffer;
    VkResult result = vk_create_command_buffer(user_context, ctx.allocator, ctx.command_pool, &command_buffer);
    if (result != VK_SUCCESS) {
        debug(user_context) << "Vulkan: vkCreateCommandBuffer returned: " << vk_get_error_name(result) << "\n";
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
        debug(user_context) << "vkBeginCommandBuffer returned " << vk_get_error_name(result) << "\n";
        return result;
    }

    // enqueue the copy operation, using the allocated buffers
    copy_helper.src = (uint64_t)(staging_buffer);
    copy_helper.dst = (uint64_t)(device_buffer);
    uint64_t src_offset = copy_helper.src_begin;
    uint64_t dst_offset = device_region->range.head_offset;
    vk_do_multidimensional_copy(user_context, command_buffer, copy_helper, src_offset, dst_offset, halide_buffer->dimensions);

    // end the command buffer
    result = vkEndCommandBuffer(command_buffer);
    if (result != VK_SUCCESS) {
        debug(user_context) << "vkEndCommandBuffer returned " << vk_get_error_name(result) << "\n";
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
        debug(user_context) << "vkQueueSubmit returned " << vk_get_error_name(result) << "\n";
        return result;
    }

    //// 14. Wait until the queue is done with the command buffer
    result = vkQueueWaitIdle(ctx.queue);
    if (result != VK_SUCCESS) {
        debug(user_context) << "vkQueueWaitIdle returned " << vk_get_error_name(result) << "\n";
        return result;
    }

    //// 15. Reclaim the staging buffer
    if (halide_can_reuse_device_allocations(user_context)) {
        ctx.allocator->release(user_context, staging_region);
    } else {
        ctx.allocator->reclaim(user_context, staging_region);
    }

    vk_destroy_command_buffer(user_context, ctx.allocator, ctx.command_pool, command_buffer);

#ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
#endif

    return 0;
}

WEAK int halide_vulkan_copy_to_host(void *user_context, halide_buffer_t *halide_buffer) {

#ifdef DEBUG_RUNTIME
    debug(user_context)
        << "halide_copy_to_host (user_context: " << user_context
        << ", halide_buffer: " << halide_buffer << ")\n";
#endif

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

    // This is the inverse of copy_to_device: we create a staging buffer, copy into
    // it, map it so the host can see it, then copy into the host buffer

    MemoryRequest request = {0};
    request.size = halide_buffer->size_in_bytes();
    request.properties.usage = MemoryUsage::TransferDst;
    request.properties.caching = MemoryCaching::UncachedCoherent;
    request.properties.visibility = MemoryVisibility::DeviceToHost;

    // allocate a new region for staging the transfer
    MemoryRegion *staging_region = ctx.allocator->reserve(user_context, request);
    if ((staging_region == nullptr) || (staging_region->handle == nullptr)) {
        error(user_context) << "Vulkan: Failed to allocate device memory!\n";
        return -1;
    }

    // retrieve the buffer from the region
    VkBuffer *staging_buffer = reinterpret_cast<VkBuffer *>(staging_region->handle);
    if (staging_buffer == nullptr) {
        error(user_context) << "Vulkan: Failed to retrieve staging buffer for device memory!\n";
        return halide_error_code_internal_error;
    }

    // get the allocated region for the device
    MemoryRegion *device_region = reinterpret_cast<MemoryRegion *>(halide_buffer->device);
    MemoryRegion *memory_region = ctx.allocator->owner_of(user_context, device_region);

    // retrieve the buffer from the region
    VkBuffer *device_buffer = reinterpret_cast<VkBuffer *>(memory_region->handle);
    if (device_buffer == nullptr) {
        error(user_context) << "Vulkan: Failed to retrieve buffer for device memory!\n";
        return halide_error_code_internal_error;
    }

    // create a command buffer
    VkCommandBuffer command_buffer;
    VkResult result = vk_create_command_buffer(user_context, ctx.allocator, ctx.command_pool, &command_buffer);
    if (result != VK_SUCCESS) {
        error(user_context) << "vk_create_command_buffer returned: " << vk_get_error_name(result) << "\n";
        return -1;
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
        error(user_context) << "vkBeginCommandBuffer returned " << vk_get_error_name(result) << "\n";
        return result;
    }

    // enqueue the copy operation, using the allocated buffers
    uint64_t copy_dst = copy_helper.dst;
    copy_helper.src = (uint64_t)(device_buffer);
    copy_helper.dst = (uint64_t)(staging_buffer);
    uint64_t src_offset = copy_helper.src_begin + device_region->range.head_offset;
    uint64_t dst_offset = 0;
    vk_do_multidimensional_copy(user_context, command_buffer, copy_helper, src_offset, dst_offset, halide_buffer->dimensions);

    // end the command buffer
    result = vkEndCommandBuffer(command_buffer);
    if (result != VK_SUCCESS) {
        error(user_context) << "vkEndCommandBuffer returned " << vk_get_error_name(result) << "\n";
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
        error(user_context) << "vkQueueSubmit returned " << vk_get_error_name(result) << "\n";
        return result;
    }

    //// 14. Wait until the queue is done with the command buffer
    result = vkQueueWaitIdle(ctx.queue);
    if (result != VK_SUCCESS) {
        error(user_context) << "vkQueueWaitIdle returned " << vk_get_error_name(result) << "\n";
        return result;
    }

    // map the staging region to a host ptr
    uint8_t *stage_host_ptr = (uint8_t *)ctx.allocator->map(user_context, staging_region);
    if (stage_host_ptr == nullptr) {
        error(user_context) << "Vulkan: Failed to map host pointer to device memory!\n";
        return halide_error_code_internal_error;
    }

    // copy to the (host-visible/coherent) staging buffer
    copy_helper.dst = copy_dst;
    copy_helper.src = (uint64_t)(stage_host_ptr);
    copy_memory(copy_helper, user_context);

    // unmap the pointer and reclaim the staging region
    ctx.allocator->unmap(user_context, staging_region);
    if (halide_can_reuse_device_allocations(user_context)) {
        ctx.allocator->release(user_context, staging_region);
    } else {
        ctx.allocator->reclaim(user_context, staging_region);
    }
    vk_destroy_command_buffer(user_context, ctx.allocator, ctx.command_pool, command_buffer);

#ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
#endif

    return 0;
}


WEAK int halide_vulkan_device_crop(void *user_context,
                                   const struct halide_buffer_t *src,
                                   struct halide_buffer_t *dst) {
    const int64_t offset = calc_device_crop_byte_offset(src, dst);
    return vk_device_crop_from_offset(user_context, src, offset, dst);
}

WEAK int halide_vulkan_device_slice(void *user_context,
                                   const struct halide_buffer_t *src,
                                   int slice_dim, int slice_pos,
                                   struct halide_buffer_t *dst) {
    const int64_t offset = calc_device_slice_byte_offset(src, slice_dim, slice_pos);
    return vk_device_crop_from_offset(user_context, src, offset, dst);
}

WEAK int halide_vulkan_device_release_crop(void *user_context,
                                           struct halide_buffer_t *halide_buffer) {

    debug(user_context)
        << "Vulkan: halide_vulkan_device_release_crop (user_context: " << user_context
        << ", halide_buffer: " << halide_buffer << ")\n";

    VulkanContext ctx(user_context);
    if (ctx.error != VK_SUCCESS) {
        return ctx.error;
    }

#ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
#endif

    halide_abort_if_false(user_context, halide_buffer->device);

    // get the allocated region for the device
    MemoryRegion *device_region = reinterpret_cast<MemoryRegion *>(halide_buffer->device);
    ctx.allocator->destroy_crop(user_context, device_region);

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
#ifdef DEBUG_RUNTIME
    debug(user_context)
        << "halide_vulkan_run (user_context: " << user_context << ", "
        << "entry: " << entry_name << ", "
        << "blocks: " << blocksX << "x" << blocksY << "x" << blocksZ << ", "
        << "threads: " << threadsX << "x" << threadsY << "x" << threadsZ << ", "
        << "shmem: " << shared_mem_bytes << "\n";
#endif

    VulkanContext ctx(user_context);
    if (ctx.error != VK_SUCCESS) {
        return ctx.error;
    }

#ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
#endif

    // Running a Vulkan pipeline requires a large number of steps
    // and boilerplate.  We save pipeline specific objects alongside the
    // shader module in the compilation cache to avoid re-creating these
    // if used more than once.
    //
    // 1. Lookup the shader module cache entry in the compilation cache
    //    1a. If shader module doesn't exist yet, then lookup invokes compile
    // 2. If the rest of the cache entry is uninitialized, then create new objects:
    //    2a. Create a descriptor set layout
    //    2b. Create the buffer for the scalar params
    //    2c. Create a pipeline layout
    //    2d. Create a compute pipeline
    //    2e. Create a descriptor set
    //    --- The above can be cached between invocations ---
    // 3. Set bindings for buffers in the descriptor set
    //    3a. Copy args into uniform buffer
    //    3b. Update buffer bindings for descriptor set
    // 4. Create a command buffer from the command pool
    // 5. Fill the command buffer with a dispatch call
    //    7a. Bind the compute pipeline
    //    7b. Bind the descriptor set
    //    7c. Add a dispatch to the command buffer
    //    7d. End the command buffer
    // 6. Submit the command buffer to our command queue
    // --- The following isn't best practice, but it's in line
    //     with what we do in Metal etc. ---
    // 7. Wait until the queue is done with the command buffer
    // 8. Cleanup all temporary objects

    //// 3. Create a compute pipeline
    // Get the shader module
    VulkanCompilationCacheEntry *cache_entry = nullptr;
    bool found = compilation_cache.lookup(ctx.device, state_ptr, cache_entry);
    halide_abort_if_false(user_context, found);
    if (cache_entry == nullptr) {
        error(user_context) << "Vulkan: Failed to locate shader module! Unable to proceed!\n";
        return halide_error_code_internal_error;
    }

    // 3a. Locate the correct entry point from the cache
    bool found_entry_point = false;
    uint32_t entry_point_index = 0;
    for (uint32_t n = 0; n < cache_entry->entry_point_count; ++n) {
        if (strstr(cache_entry->entry_point_data[n].entry_point_name, entry_name) != nullptr) {
            entry_point_index = n;
            found_entry_point = true;
        }
    }
    if (!found_entry_point) {
        error(user_context) << "Vulkan: Failed to locate shader entry point! Unable to proceed!\n";
        return halide_error_code_internal_error;
    }
    debug(user_context) << " found entry point ["
                        << (entry_point_index + 1) << " of " << cache_entry->entry_point_count
                        << "] '" << entry_name << "'\n";

    halide_abort_if_false(user_context, cache_entry->descriptor_set_layouts != nullptr);
    if (cache_entry->pipeline_layout == 0) {

        //// 1. Create all descriptor set layouts
        for (uint32_t n = 0; n < cache_entry->entry_point_count; ++n) {
            if (((void *)cache_entry->descriptor_set_layouts[n]) == nullptr) {
                uint32_t uniform_buffer_count = cache_entry->entry_point_data[n].uniform_buffer_count;
                uint32_t storage_buffer_count = cache_entry->entry_point_data[n].storage_buffer_count;
                debug(user_context) << " creating descriptor set layout [" << n << "] " << cache_entry->entry_point_data[n].entry_point_name << "\n";
                VkResult result = vk_create_descriptor_set_layout(user_context, ctx.allocator, uniform_buffer_count, storage_buffer_count, &(cache_entry->descriptor_set_layouts[n]));
                if (result != VK_SUCCESS) {
                    error(user_context) << "vk_create_descriptor_set_layout() failed! Unable to create shader module! Error: " << vk_get_error_name(result) << "\n";
                    return result;
                }
            }
        }

        ///// 2. Create the pipeline layout
        VkResult result = vk_create_pipeline_layout(user_context, ctx.allocator, cache_entry->entry_point_count, cache_entry->descriptor_set_layouts, &(cache_entry->pipeline_layout));
        if (result != VK_SUCCESS) {
            error(user_context) << "vk_create_pipeline_layout() failed! Unable to create shader module! Error: " << vk_get_error_name(result) << "\n";
            return halide_error_code_internal_error;
        }
    }

    VulkanEntryPointData *entry_point_data = (cache_entry->entry_point_data + entry_point_index);
    halide_abort_if_false(user_context, entry_point_data != nullptr);

    //// 1a. Create a buffer for the scalar parameters
    if ((entry_point_data->args_region == nullptr) && entry_point_data->uniform_buffer_count) {
        size_t scalar_buffer_size = vk_estimate_scalar_uniform_buffer_size(user_context, arg_sizes, args, arg_is_buffer);
        if (scalar_buffer_size > 0) {
            entry_point_data->args_region = vk_create_scalar_uniform_buffer(user_context, ctx.allocator, scalar_buffer_size);
            if (entry_point_data->args_region == nullptr) {
                error(user_context) << "vk_create_scalar_uniform_buffer() failed! Unable to create shader module!\n";
                return halide_error_code_internal_error;
            }
        }
    }

    // Construct the pipeline
    if (entry_point_data->compute_pipeline == 0) {
        VkResult result = vk_create_compute_pipeline(user_context, ctx.allocator, entry_name, cache_entry->shader_module, cache_entry->pipeline_layout, &(entry_point_data->compute_pipeline));
        if (result != VK_SUCCESS) {
            error(user_context) << "vk_create_compute_pipeline() failed! Unable to proceed! Error: " << vk_get_error_name(result) << "\n";
            return halide_error_code_internal_error;
        }
    }

    //// 4. Create a descriptor set
    if (entry_point_data->descriptor_set == 0) {

        // Construct a descriptor pool
        //
        // NOTE: while this could be re-used across multiple pipelines, we only know the storage requirements of this kernel's
        //       inputs and outputs ... so create a pool specific to the number of buffers known at this time

        uint32_t uniform_buffer_count = entry_point_data->uniform_buffer_count;
        uint32_t storage_buffer_count = entry_point_data->storage_buffer_count;
        VkResult result = vk_create_descriptor_pool(user_context, ctx.allocator, uniform_buffer_count, storage_buffer_count, &(entry_point_data->descriptor_pool));
        if (result != VK_SUCCESS) {
            error(user_context) << "vk_create_descriptor_pool() failed! Unable to proceed! Error: " << vk_get_error_name(result) << "\n";
            return result;
        }

        // Create the descriptor set
        result = vk_create_descriptor_set(user_context, ctx.allocator, cache_entry->descriptor_set_layouts[entry_point_index], entry_point_data->descriptor_pool, &(entry_point_data->descriptor_set));
        if (result != VK_SUCCESS) {
            error(user_context) << "vk_create_descriptor_pool() failed! Unable to proceed! Error: " << vk_get_error_name(result) << "\n";
            return result;
        }
    }

    //// 5. Update uniform args and bindings for buffers in the descriptor set
    VkBuffer *args_buffer = nullptr;
    if ((entry_point_data->args_region != nullptr) && entry_point_data->uniform_buffer_count) {
        VkResult result = vk_update_scalar_uniform_buffer(user_context, ctx.allocator, entry_point_data->args_region, arg_sizes, args, arg_is_buffer);
        if (result != VK_SUCCESS) {
            debug(user_context) << "vk_update_scalar_uniform_buffer() failed! Unable to proceed! Error: " << vk_get_error_name(result) << "\n";
            return result;
        }

        args_buffer = reinterpret_cast<VkBuffer *>(entry_point_data->args_region->handle);
        if (args_buffer == nullptr) {
            error(user_context) << "Vulkan: Failed to retrieve scalar args buffer for device memory!\n";
            return halide_error_code_internal_error;
        }
    }

    VkResult result = vk_update_descriptor_set(user_context, ctx.allocator, args_buffer, entry_point_data->uniform_buffer_count, entry_point_data->storage_buffer_count, arg_sizes, args, arg_is_buffer, entry_point_data->descriptor_set);
    if (result != VK_SUCCESS) {
        debug(user_context) << "vk_update_descriptor_set() failed! Unable to proceed! Error: " << vk_get_error_name(result) << "\n";
        return result;
    }

    //// 6. Create a command buffer from the command pool
    VkCommandBuffer command_buffer;
    result = vk_create_command_buffer(user_context, ctx.allocator, ctx.command_pool, &command_buffer);
    if (result != VK_SUCCESS) {
        debug(user_context) << "vk_create_command_buffer() failed! Unable to proceed! Error: " << vk_get_error_name(result) << "\n";
        return result;
    }

    //// 7. Begin the command buffer
    result = vk_fill_command_buffer_with_dispatch_call(user_context,
                                                       ctx.device, command_buffer,
                                                       entry_point_data->compute_pipeline,
                                                       cache_entry->pipeline_layout,
                                                       entry_point_data->descriptor_set,
                                                       entry_point_index,
                                                       blocksX, blocksY, blocksZ);
    if (result != VK_SUCCESS) {
        debug(user_context) << "vk_fill_command_buffer_with_dispatch_call() failed! Unable to proceed! Error: " << vk_get_error_name(result) << "\n";
        return result;
    }

    //// 8. Submit the command buffer to our command queue
    result = vk_submit_command_buffer(user_context, ctx.queue, command_buffer);
    if (result != VK_SUCCESS) {
        debug(user_context) << "vk_submit_command_buffer() failed! Unable to proceed! Error: " << vk_get_error_name(result) << "\n";
        return result;
    }

    //// 9. Wait until the queue is done with the command buffer
    result = vkQueueWaitIdle(ctx.queue);
    if (result != VK_SUCCESS) {
        debug(user_context) << "vkQueueWaitIdle returned " << vk_get_error_name(result) << "\n";
        return result;
    }

    //// 10. Cleanup
    vk_destroy_command_buffer(user_context, ctx.allocator, ctx.command_pool, command_buffer);
    vkResetCommandPool(ctx.device, ctx.command_pool, VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT);

#ifdef DEBUG_RUNTIME
    debug(user_context) << "halide_vulkan_run: blocks_allocated="
                        << (uint32_t)ctx.allocator->blocks_allocated() << " "
                        << "bytes_allocated_for_blocks=" << (uint32_t)ctx.allocator->bytes_allocated_for_blocks() << " "
                        << "regions_allocated=" << (uint32_t)ctx.allocator->regions_allocated() << " "
                        << "bytes_allocated_for_regions=" << (uint32_t)ctx.allocator->bytes_allocated_for_regions() << " "
                        << "\n";

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

WEAK halide_device_allocation_pool vulkan_allocation_pool;

WEAK int halide_vulkan_release_unused_device_allocations(void *user_context) {
    debug(user_context)
        << "halide_vulkan_release_unused_device_allocations (user_context: " << user_context
        << ")\n";

    VulkanContext ctx(user_context);
    if (ctx.error != VK_SUCCESS) {
        return -1;
    }

    // collect all unused allocations
    ctx.allocator->collect(user_context);
    return 0;
}

namespace {

WEAK __attribute__((constructor)) void register_vulkan_allocation_pool() {
    vulkan_allocation_pool.release_unused = &halide_vulkan_release_unused_device_allocations;
    halide_register_device_allocation_pool(&vulkan_allocation_pool);
}

WEAK __attribute__((destructor)) void halide_vulkan_cleanup() {
    halide_vulkan_device_release(nullptr);
}

// --------------------------------------------------------------------------

}  // namespace

// --------------------------------------------------------------------------

}  // extern "C" linkage

// --------------------------------------------------------------------------

namespace Halide {
namespace Runtime {
namespace Internal {
namespace Vulkan {

// --------------------------------------------------------------------------

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
    halide_vulkan_device_crop,
    halide_vulkan_device_slice,
    halide_vulkan_device_release_crop,
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

// --------------------------------------------------------------------------

}  // namespace Vulkan
}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide
