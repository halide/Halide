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
WEAK int halide_default_vulkan_acquire_context(void *user_context,
                                               halide_vulkan_memory_allocator **allocator,
                                               VkInstance *instance,
                                               VkDevice *device,
                                               VkPhysicalDevice *physical_device,
                                               VkQueue *queue,
                                               uint32_t *queue_family_index,
                                               VkDebugUtilsMessengerEXT *messenger,
                                               bool create) {
#ifdef DEBUG_RUNTIME
    halide_start_clock(user_context);
#endif
    halide_debug_assert(user_context, instance != nullptr);
    halide_debug_assert(user_context, device != nullptr);
    halide_debug_assert(user_context, queue != nullptr);
    halide_debug_assert(user_context, &thread_lock != nullptr);
    halide_mutex_lock(&thread_lock);

    // If the context has not been initialized, initialize it now.
    if ((cached_instance == nullptr) && create) {
        int error_code = vk_create_context(user_context,
                                           reinterpret_cast<VulkanMemoryAllocator **>(&cached_allocator),
                                           &cached_instance,
                                           &cached_device,
                                           &cached_physical_device,
                                           &cached_queue,
                                           &cached_queue_family_index,
                                           &cached_messenger);
        if (error_code != halide_error_code_success) {
            debug(user_context) << "halide_vulkan_acquire_context: FAILED to create context!\n";
            halide_mutex_unlock(&thread_lock);
            return error_code;
        }
    }

    *allocator = cached_allocator;
    *instance = cached_instance;
    *device = cached_device;
    *physical_device = cached_physical_device;
    *queue = cached_queue;
    *queue_family_index = cached_queue_family_index;
    *messenger = cached_messenger;
    return halide_error_code_success;
}

WEAK int halide_default_vulkan_release_context(void *user_context, VkInstance instance, VkDevice device, VkQueue queue, VkDebugUtilsMessengerEXT messenger) {
    halide_mutex_unlock(&thread_lock);
    return halide_error_code_success;
}

}  // extern "C"

namespace Halide {
namespace Runtime {
namespace Internal {
namespace Vulkan {

WEAK halide_vulkan_acquire_context_t acquire_context = halide_default_vulkan_acquire_context;
WEAK halide_vulkan_release_context_t release_context = halide_default_vulkan_release_context;

}  // namespace Vulkan
}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

extern "C" {

WEAK int halide_vulkan_acquire_context(void *user_context,
                                       halide_vulkan_memory_allocator **allocator,
                                       VkInstance *instance,
                                       VkDevice *device,
                                       VkPhysicalDevice *physical_device,
                                       VkQueue *queue,
                                       uint32_t *queue_family_index,
                                       VkDebugUtilsMessengerEXT *messenger,
                                       bool create) {
    return Vulkan::acquire_context(user_context, allocator, instance, device,
                                   physical_device, queue, queue_family_index, messenger, create);
}

WEAK halide_vulkan_acquire_context_t halide_set_vulkan_acquire_context(halide_vulkan_acquire_context_t handler) {
    halide_vulkan_acquire_context_t result = Vulkan::acquire_context;
    Vulkan::acquire_context = handler;
    return result;
}

WEAK int halide_vulkan_release_context(void *user_context, VkInstance instance, VkDevice device, VkQueue queue, VkDebugUtilsMessengerEXT messenger) {
    return Vulkan::release_context(user_context, instance, device, queue, messenger);
}

WEAK halide_vulkan_release_context_t halide_set_vulkan_release_context(halide_vulkan_release_context_t handler) {
    halide_vulkan_release_context_t result = Vulkan::release_context;
    Vulkan::release_context = handler;
    return result;
}

WEAK bool halide_vulkan_is_initialized() {
    halide_mutex_lock(&thread_lock);
    bool is_initialized = (cached_instance != nullptr) && (cached_device != nullptr);
    halide_mutex_unlock(&thread_lock);
    return is_initialized;
}

WEAK int halide_vulkan_device_free(void *user_context, halide_buffer_t *halide_buffer) {
    debug(user_context)
        << "halide_vulkan_device_free (user_context: " << user_context
        << ", halide_buffer: " << halide_buffer << ")\n";

    // halide_vulkan_device_free, at present, can be exposed to clients and they
    // should be allowed to call halide_vulkan_device_free on any halide_buffer_t
    // including ones that have never been used with a GPU.
    if (halide_buffer->device == 0) {
        return halide_error_code_success;
    }

    VulkanContext ctx(user_context);
    if (ctx.error != halide_error_code_success) {
        error(user_context) << "Vulkan: Failed to acquire context!\n";
        return ctx.error;
    }

#ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
#endif

    // get the allocated region for the device
    MemoryRegion *device_region = reinterpret_cast<MemoryRegion *>(halide_buffer->device);
    MemoryRegion *memory_region = ctx.allocator->owner_of(user_context, device_region);
    if (ctx.allocator && memory_region && memory_region->handle) {
        if (halide_can_reuse_device_allocations(user_context)) {
            // Work still in flight may use the region: only work submitted after it can reuse it.
            ctx.allocator->release(user_context, memory_region);
        } else {
            // Wait for any work in flight that may still use the region.
            int error_code = vk_wait_for_submissions(user_context, ctx.allocator);
            if (error_code != halide_error_code_success) {
                error(user_context) << "Vulkan: Failed to wait for submitted work!\n";
                return error_code;
            }
            ctx.allocator->reclaim(user_context, memory_region);
        }
    }
    halide_buffer->device = 0;
    halide_buffer->device_interface->impl->release_module();
    halide_buffer->device_interface = nullptr;

#ifdef DEBUG_RUNTIME
    debug(user_context) << "Vulkan: Released memory for device region ("
                        << "user_context: " << user_context << ", "
                        << "buffer: " << halide_buffer << ", "
                        << "size_in_bytes: " << (uint64_t)device_region->allocation.size << ")\n";

    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
#endif

    return halide_error_code_success;
}

WEAK int halide_vulkan_compute_capability(void *user_context, int *major, int *minor) {
    debug(user_context) << " halide_vulkan_compute_capability (user_context: " << user_context << ")\n";
    return vk_find_compute_capability(user_context, major, minor);
}

WEAK int halide_vulkan_initialize_kernels(void *user_context, void **state_ptr, const char *src, int size) {
    debug(user_context)
        << "halide_vulkan_init_kernels (user_context: " << user_context
        << ", state_ptr: " << state_ptr
        << ", program: " << (void *)src
        << ", size: " << size << "\n";

    VulkanContext ctx(user_context);
    if (ctx.error != halide_error_code_success) {
        error(user_context) << "Vulkan: Failed to acquire context!\n";
        return ctx.error;
    }

#ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
#endif
    debug(user_context) << "halide_vulkan_initialize_kernels got compilation_cache mutex.\n";

    VulkanCompilationCacheEntry *cache_entry = nullptr;
    if (!compilation_cache.kernel_state_setup(user_context, state_ptr, ctx.device, cache_entry,
                                              Halide::Runtime::Internal::Vulkan::vk_compile_kernel_module,
                                              user_context, ctx.allocator, src, size)) {
        error(user_context) << "Vulkan: Failed to setup compilation cache!\n";
        return halide_error_code_generic_error;
    }

#ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
#endif

    return halide_error_code_success;
}

WEAK void halide_vulkan_finalize_kernels(void *user_context, void *state_ptr) {
    debug(user_context)
        << "halide_vulkan_finalize_kernels (user_context: " << user_context
        << ", state_ptr: " << state_ptr << "\n";

#ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
#endif

    VulkanContext ctx(user_context);
    if (ctx.error == halide_error_code_success) {
        // Submit the work the pipeline recorded if the GPU is running out of work.
        // Otherwise it is submitted together with what is recorded next, or by
        // the next wait (e.g. halide_vulkan_device_sync).
        if (vk_flush_submissions_if_gpu_needs_work(user_context, ctx.allocator) != halide_error_code_success) {
            error(user_context) << "Vulkan: Failed to submit recorded work!\n";
        }
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
    if (ctx.error != halide_error_code_success) {
        error(user_context) << "Vulkan: Failed to acquire context!\n";
        return ctx.error;
    }

#ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
#endif

    int error_code = vk_wait_for_submissions(user_context, ctx.allocator);
    if (error_code != halide_error_code_success) {
        error(user_context) << "Vulkan: Failed to wait for submitted work!\n";
        return error_code;
    }

#ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
#endif

    return halide_error_code_success;
}

WEAK int halide_vulkan_device_release(void *user_context) {
    debug(user_context)
        << "halide_vulkan_device_release (user_context: " << user_context << ")\n";

    VulkanMemoryAllocator *allocator = nullptr;
    VkInstance instance = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t queue_family_index = 0;
    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;

    int destroy_status = halide_error_code_success;
    int acquire_status = halide_vulkan_acquire_context(user_context,
                                                       reinterpret_cast<halide_vulkan_memory_allocator **>(&allocator),
                                                       &instance, &device, &physical_device, &queue, &queue_family_index, &messenger, false);

    if (acquire_status == halide_error_code_success) {
        destroy_status = vk_wait_for_submissions(user_context, allocator);

        // Destroy the context if we created it
        if ((instance == cached_instance) && (device == cached_device)) {
            int error_code = vk_destroy_context(user_context, allocator, instance, device, physical_device, queue, messenger);
            cached_allocator = nullptr;
            cached_device = VK_NULL_HANDLE;
            cached_physical_device = VK_NULL_HANDLE;
            cached_queue = VK_NULL_HANDLE;
            cached_queue_family_index = 0;
            cached_instance = VK_NULL_HANDLE;
            cached_messenger = VK_NULL_HANDLE;
            if (destroy_status == halide_error_code_success) {
                destroy_status = error_code;
            }
        }

        halide_vulkan_release_context(user_context, instance, device, queue, messenger);
    }

    return destroy_status;
}

WEAK int halide_vulkan_device_malloc(void *user_context, halide_buffer_t *buf) {
    debug(user_context)
        << "halide_vulkan_device_malloc (user_context: " << user_context
        << ", buf: " << buf << ")\n";

    VulkanContext ctx(user_context);
    if (ctx.error != halide_error_code_success) {
        error(user_context) << "Vulkan: Failed to acquire context!\n";
        return ctx.error;
    }

    size_t size = buf->size_in_bytes();
    if (buf->device) {
        MemoryRegion *device_region = (MemoryRegion *)(buf->device);
        if (device_region->allocation.size >= size) {
            debug(user_context) << "Vulkan: Requested allocation for existing device memory ... using existing buffer!\n";
            return halide_error_code_success;
        } else {
            debug(user_context) << "Vulkan: Requested allocation of different size ... reallocating buffer!\n";
            if (halide_can_reuse_device_allocations(user_context)) {
                ctx.allocator->release(user_context, device_region);
            } else {
                int error_code = vk_wait_for_submissions(user_context, ctx.allocator);
                if (error_code != halide_error_code_success) {
                    error(user_context) << "Vulkan: Failed to wait for submitted work!\n";
                    return error_code;
                }
                ctx.allocator->reclaim(user_context, device_region);
            }
            buf->device = 0;
        }
    }

    for (int i = 0; i < buf->dimensions; i++) {
        halide_debug_assert(user_context, buf->dim[i].stride >= 0);
    }

#ifdef DEBUG_RUNTIME
    debug(user_context) << "    allocating buffer: ";
    if (buf && buf->dim) {
        debug(user_context) << "extents: ";
        for (int i = 0; i < buf->dimensions; i++) {
            debug(user_context) << buf->dim[i].extent << " ";
        }
        debug(user_context) << "strides: ";
        for (int i = 0; i < buf->dimensions; i++) {
            debug(user_context) << buf->dim[i].stride << " ";
        }
    }
    debug(user_context) << "type: " << buf->type << " "
                        << "size_in_bytes: " << (uint64_t)size << " "
                        << "(or " << (size * 1e-6f) << "MB)\n";

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
        return halide_error_code_device_malloc_failed;
    }

    buf->device = (uint64_t)device_region;
    buf->device_interface = &vulkan_device_interface;
    buf->device_interface->impl->use_module();

#ifdef DEBUG_RUNTIME
    debug(user_context)
        << "    allocated device region=" << (void *)device_region << "\n"
        << "    containing device buffer=" << (void *)device_region->handle << "\n"
        << "    for halide buffer " << buf << "\n";
#endif

    // retrieve the buffer from the region
    VkBuffer *device_buffer = reinterpret_cast<VkBuffer *>(device_region->handle);
    if (device_buffer == nullptr) {
        error(user_context) << "Vulkan: Failed to retrieve device buffer for device memory!\n";
        return halide_error_code_internal_error;
    }

    VulkanSubmission *submission = vk_begin_submission(user_context, ctx.allocator, ctx.queue, ctx.queue_family_index);
    if (submission == nullptr) {
        error(user_context) << "Vulkan: Failed to begin a submission!\n";
        return halide_error_code_device_malloc_failed;
    }

    vk_clear_device_buffer(submission->command_buffer, *device_buffer);
    int error_code = vk_end_submission(user_context, ctx.allocator, submission);
    if (error_code != halide_error_code_success) {
        error(user_context) << "Vulkan: Failed to clear device buffer!\n";
    }

#ifdef DEBUG_RUNTIME
    debug(user_context) << "Vulkan: Reserved memory for device region ("
                        << "user_context: " << user_context << ", "
                        << "buffer: " << buf << ", "
                        << "size_in_bytes: " << (uint64_t)size << ")\n";

    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
#endif

    return error_code;
}

WEAK int halide_vulkan_copy_to_device(void *user_context, halide_buffer_t *halide_buffer) {
    int error_code = halide_vulkan_device_malloc(user_context, halide_buffer);
    if (error_code != halide_error_code_success) {
        error(user_context) << "Vulkan: Failed to allocate device memory!\n";
        return error_code;
    }

    debug(user_context)
        << "halide_vulkan_copy_to_device (user_context: " << user_context
        << ", halide_buffer: " << halide_buffer << ")\n";

    // Acquire the context so we can use the command queue.
    VulkanContext ctx(user_context);
    if (ctx.error != halide_error_code_success) {
        error(user_context) << "Vulkan: Failed to acquire context!\n";
        return ctx.error;
    }

#ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
#endif

    if ((halide_buffer->host == nullptr) || (halide_buffer->device == 0)) {
        error(user_context) << "Vulkan: Missing host/device pointers for halide buffer!\n";
        return halide_error_code_internal_error;
    }
    device_copy copy_helper = make_host_to_device_copy(halide_buffer);

    // We construct a staging buffer to copy into from host memory.  Then,
    // we use vkCmdCopyBuffer() to copy from the staging buffer into the
    // the actual device memory. The staging buffer is released once that
    // copy has finished.
    error_code = vk_throttle_staging(user_context, ctx.allocator, halide_buffer->size_in_bytes());
    if (error_code != halide_error_code_success) {
        error(user_context) << "Vulkan: Failed to wait for submitted work!\n";
        return error_code;
    }

    MemoryRequest request = {0};
    request.size = halide_buffer->size_in_bytes();
    request.properties.usage = MemoryUsage::TransferSrc;
    request.properties.caching = MemoryCaching::UncachedCoherent;
    request.properties.visibility = MemoryVisibility::HostToDevice;

    // allocate a new region
    ScopedStagingRegion staging(user_context, ctx.allocator);
    staging.region = ctx.allocator->reserve(user_context, request);
    MemoryRegion *staging_region = staging.region;
    if ((staging_region == nullptr) || (staging_region->handle == nullptr)) {
        error(user_context) << "Vulkan: Failed to allocate device memory!\n";
        return halide_error_code_device_malloc_failed;
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
    error_code = ctx.allocator->unmap(user_context, staging_region);
    if (error_code != halide_error_code_success) {
        error(user_context) << "Vulkan: Failed to unmap host pointer to device memory!\n";
        return error_code;
    }

    // get the allocated region for the device
    MemoryRegion *device_region = reinterpret_cast<MemoryRegion *>(halide_buffer->device);
    if (device_region == nullptr) {
        error(user_context) << "Vulkan: Failed to retrieve device region for buffer!\n";
        return halide_error_code_internal_error;
    }

    MemoryRegion *memory_region = ctx.allocator->owner_of(user_context, device_region);
    if (memory_region == nullptr) {
        error(user_context) << "Vulkan: Failed to retrieve memory region for device!\n";
        return halide_error_code_internal_error;
    }

    // retrieve the buffer from the region
    VkBuffer *device_buffer = reinterpret_cast<VkBuffer *>(memory_region->handle);
    if (device_buffer == nullptr) {
        error(user_context) << "Vulkan: Failed to retrieve buffer for device memory!\n";
        return halide_error_code_internal_error;
    }

#ifdef DEBUG_RUNTIME
    debug(user_context)
        << "  copying into device region=" << (void *)device_region << "\n"
        << "  containing device buffer=" << (void *)device_buffer << "\n"
        << "  from halide buffer=" << halide_buffer << "\n";
#endif

    VulkanSubmission *submission = vk_begin_submission(user_context, ctx.allocator, ctx.queue, ctx.queue_family_index);
    if (submission == nullptr) {
        error(user_context) << "Vulkan: Failed to begin a submission!\n";
        return halide_error_code_device_buffer_copy_failed;
    }

    // define the src and dst config
    bool from_host = true;
    bool to_host = false;

    uint64_t src_offset = copy_helper.src_begin;
    uint64_t dst_offset = copy_helper.dst_begin + (device_region->indexing.offset * halide_buffer->type.bytes());

    copy_helper.src = (uint64_t)(staging_buffer);
    copy_helper.dst = (uint64_t)(device_buffer);

    // enqueue the copy operation, using the allocated buffers
    error_code = vk_do_multidimensional_copy(user_context, submission->command_buffer, copy_helper,
                                             src_offset, dst_offset,
                                             halide_buffer->dimensions,
                                             from_host, to_host);

    if (error_code != halide_error_code_success) {
        error(user_context) << "Vulkan: vk_do_multidimensional_copy failed!\n";
        return error_code;
    }

    // end the copy: the submission releases the staging buffer once it is done
    vk_attach_staging_region(submission, staging.take(), request.size);
    error_code = vk_end_submission(user_context, ctx.allocator, submission);
    if (error_code != halide_error_code_success) {
        error(user_context) << "Vulkan: Failed to submit the copy!\n";
        return halide_error_code_device_buffer_copy_failed;
    }

#ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
#endif

    return halide_error_code_success;
}

WEAK int halide_vulkan_copy_to_host(void *user_context, halide_buffer_t *halide_buffer) {
#ifdef DEBUG_RUNTIME
    debug(user_context)
        << "halide_copy_to_host (user_context: " << user_context
        << ", halide_buffer: " << halide_buffer << ")\n";
#endif
    if (halide_buffer == nullptr) {
        error(user_context) << "Vulkan: Failed to copy buffer to host ... invalid halide buffer!\n";
        return halide_error_code_copy_to_host_failed;
    }

    // Acquire the context so we can use the command queue. This also avoids multiple
    // redundant calls to enqueue a download when multiple threads are trying to copy
    // the same buffer.
    VulkanContext ctx(user_context);
    if (ctx.error != halide_error_code_success) {
        error(user_context) << "Vulkan: Failed to acquire context!\n";
        return ctx.error;
    }

#ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
#endif
    if ((halide_buffer->host == nullptr) || (halide_buffer->device == 0)) {
        error(user_context) << "Vulkan: Missing host/device pointers for halide buffer!\n";
        return halide_error_code_internal_error;
    }

    device_copy copy_helper = make_device_to_host_copy(halide_buffer);

    // This is the inverse of copy_to_device: we create a staging buffer, copy into
    // it, map it so the host can see it, then copy into the host buffer
    MemoryRequest request = {0};
    request.size = halide_buffer->size_in_bytes();
    request.properties.usage = MemoryUsage::TransferDst;
    request.properties.caching = MemoryCaching::UncachedCoherent;
    request.properties.visibility = MemoryVisibility::DeviceToHost;

    // allocate a new region for staging the transfer
    ScopedStagingRegion staging(user_context, ctx.allocator);
    staging.region = ctx.allocator->reserve(user_context, request);
    MemoryRegion *staging_region = staging.region;
    if ((staging_region == nullptr) || (staging_region->handle == nullptr)) {
        error(user_context) << "Vulkan: Failed to allocate device memory!\n";
        return halide_error_code_device_malloc_failed;
    }

    // retrieve the buffer from the region
    VkBuffer *staging_buffer = reinterpret_cast<VkBuffer *>(staging_region->handle);
    if (staging_buffer == nullptr) {
        error(user_context) << "Vulkan: Failed to retrieve staging buffer for device memory!\n";
        return halide_error_code_internal_error;
    }

    // get the allocated region for the device
    MemoryRegion *device_region = reinterpret_cast<MemoryRegion *>(halide_buffer->device);
    if (device_region == nullptr) {
        error(user_context) << "Vulkan: Failed to retrieve device region for buffer!\n";
        return halide_error_code_internal_error;
    }

    MemoryRegion *memory_region = ctx.allocator->owner_of(user_context, device_region);
    if (memory_region == nullptr) {
        error(user_context) << "Vulkan: Failed to retrieve memory region for buffer!\n";
        return halide_error_code_internal_error;
    }

    // retrieve the buffer from the region
    VkBuffer *device_buffer = reinterpret_cast<VkBuffer *>(memory_region->handle);
    if (device_buffer == nullptr) {
        error(user_context) << "Vulkan: Failed to retrieve buffer for device memory!\n";
        return halide_error_code_internal_error;
    }

#ifdef DEBUG_RUNTIME
    debug(user_context)
        << "  copying from device region=" << (void *)device_region << "\n"
        << "  containing device buffer=" << (void *)device_buffer << "\n"
        << "  into halide buffer=" << halide_buffer << "\n";
#endif

    VulkanSubmission *submission = vk_begin_submission(user_context, ctx.allocator, ctx.queue, ctx.queue_family_index);
    if (submission == nullptr) {
        error(user_context) << "Vulkan: Failed to begin a submission!\n";
        return halide_error_code_copy_to_host_failed;
    }

    // define the src and dst config
    bool from_host = false;
    bool to_host = true;
    uint64_t copy_dst = copy_helper.dst;
    uint64_t src_offset = copy_helper.src_begin + (device_region->indexing.offset * halide_buffer->type.bytes());
    uint64_t dst_offset = copy_helper.dst_begin;

    copy_helper.src = (uint64_t)(device_buffer);
    copy_helper.dst = (uint64_t)(staging_buffer);

    // enqueue the copy operation, using the allocated buffers
    int error_code = vk_do_multidimensional_copy(user_context, submission->command_buffer, copy_helper,
                                                 src_offset, dst_offset,
                                                 halide_buffer->dimensions,
                                                 from_host, to_host);

    if (error_code != halide_error_code_success) {
        error(user_context) << "Vulkan: vk_do_multidimensional_copy failed!\n";
        return error_code;
    }

    // submit the copy, and wait for it (and everything before it) to finish
    error_code = vk_end_submission(user_context, ctx.allocator, submission, true);
    if (error_code == halide_error_code_success) {
        error_code = vk_wait_for_submissions(user_context, ctx.allocator);
    }
    if (error_code != halide_error_code_success) {
        error(user_context) << "Vulkan: Failed to submit the copy!\n";
        return halide_error_code_copy_to_host_failed;
    }

    // map the staging region to a host ptr
    uint8_t *stage_host_ptr = (uint8_t *)ctx.allocator->map(user_context, staging_region);
    if (stage_host_ptr == nullptr) {
        error(user_context) << "Vulkan: Failed to map host pointer to device memory!\n";
        return halide_error_code_copy_to_device_failed;
    }

    // copy to the (host-visible/coherent) staging buffer
    copy_helper.dst = copy_dst;
    copy_helper.src = (uint64_t)(stage_host_ptr);
    copy_memory(copy_helper, user_context);

    // unmap the pointer (the staging region is released when it goes out of scope)
    error_code = ctx.allocator->unmap(user_context, staging_region);
    if (error_code != halide_error_code_success) {
        error(user_context) << "Vulkan: Failed to umap staging region!\n";
        return error_code;
    }

#ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
#endif

    return halide_error_code_success;
}

WEAK int halide_vulkan_buffer_copy(void *user_context, struct halide_buffer_t *src,
                                   const struct halide_device_interface_t *dst_device_interface,
                                   struct halide_buffer_t *dst) {
    if (dst->dimensions > MAX_COPY_DIMS) {
        error(user_context) << "Vulkan: Buffer has too many dimensions to copy to/from GPU\n";
        return halide_error_code_buffer_extents_too_large;
    }

    // We only handle copies to Vulkan buffers or to host
    if ((dst_device_interface != nullptr) && (dst_device_interface != &vulkan_device_interface)) {
        error(user_context) << "Vulkan: Unable to copy buffer ... only Vulkan allocated device buffers copying to/from host are supported!\n";
        return halide_error_code_device_buffer_copy_failed;
    }

    if ((src->device_dirty() || src->host == nullptr) && (src->device_interface != &vulkan_device_interface)) {
        // This is handled at the higher level.
        return halide_error_code_incompatible_device_interface;
    }

    bool from_host = (src->device_interface != &vulkan_device_interface) ||
                     (src->device == 0) ||
                     (src->host_dirty() && src->host != nullptr);
    bool to_host = !dst_device_interface;

    if (!(from_host || src->device)) {
        error(user_context) << "Vulkan: halide_vulkan_buffer_copy: invalid copy source\n";
        return halide_error_code_device_buffer_copy_failed;
    }
    if (!(to_host || dst->device)) {
        error(user_context) << "Vulkan: halide_vulkan_buffer_copy: invalid copy destination\n";
        return halide_error_code_device_buffer_copy_failed;
    }

    device_copy copy_helper = make_buffer_copy(src, from_host, dst, to_host);

    int error_code = halide_error_code_success;
    {
        VulkanContext ctx(user_context);
        if (ctx.error != halide_error_code_success) {
            error(user_context) << "Vulkan: Failed to acquire context!\n";
            return ctx.error;
        }

        debug(user_context)
            << "halide_vulkan_buffer_copy (user_context: " << user_context
            << ", src: " << src << ", dst: " << dst << ")\n";

#ifdef DEBUG_RUNTIME
        uint64_t t_before = halide_current_time_ns(user_context);
#endif
        ScopedStagingRegion staging(user_context, ctx.allocator);
        MemoryRegion *src_buffer_region = nullptr;
        MemoryRegion *dst_buffer_region = nullptr;

        int error_code = halide_error_code_success;
        if (!from_host && !to_host) {
            // Device only case
            debug(user_context) << " buffer copy from: device to: device\n";

            // get the buffer regions for the device
            src_buffer_region = reinterpret_cast<MemoryRegion *>(src->device);
            dst_buffer_region = reinterpret_cast<MemoryRegion *>(dst->device);

        } else if (!from_host && to_host) {
            // Device to Host
            debug(user_context) << " buffer copy from: device to: host\n";

            // Need to make sure all reads and writes to/from source are complete.
            MemoryRequest request = {0};
            request.size = src->size_in_bytes();

            // NOTE: We may re-use this buffer so enable both src and dst
            request.properties.usage = MemoryUsage::TransferSrcDst;
            request.properties.caching = MemoryCaching::UncachedCoherent;
            request.properties.visibility = MemoryVisibility::DeviceToHost;

            // allocate a new region
            staging.region = ctx.allocator->reserve(user_context, request);
            if ((staging.region == nullptr) || (staging.region->handle == nullptr)) {
                error(user_context) << "Vulkan: Failed to allocate device memory!\n";
                return halide_error_code_device_malloc_failed;
            }

            // use the staging region and buffer from the copy destination
            src_buffer_region = reinterpret_cast<MemoryRegion *>(src->device);
            dst_buffer_region = staging.region;

        } else if (from_host && !to_host) {
            // Host to Device
            debug(user_context) << " buffer copy from: host to: device\n";

            // Need to make sure all reads and writes to/from destination are complete.
            MemoryRequest request = {0};
            request.size = src->size_in_bytes();

            // NOTE: We may re-use this buffer so enable both src and dst
            request.properties.usage = MemoryUsage::TransferSrcDst;
            request.properties.caching = MemoryCaching::UncachedCoherent;
            request.properties.visibility = MemoryVisibility::HostToDevice;

            // wait for earlier uploads rather than have too many staging buffers in flight
            error_code = vk_throttle_staging(user_context, ctx.allocator, request.size);
            if (error_code != halide_error_code_success) {
                error(user_context) << "Vulkan: Failed to wait for submitted work!\n";
                return error_code;
            }

            // allocate a new region
            staging.region = ctx.allocator->reserve(user_context, request);
            if ((staging.region == nullptr) || (staging.region->handle == nullptr)) {
                error(user_context) << "Vulkan: Failed to allocate device memory!\n";
                return halide_error_code_device_malloc_failed;
            }

            // map the region to a host ptr
            uint8_t *stage_host_ptr = (uint8_t *)ctx.allocator->map(user_context, staging.region);
            if (stage_host_ptr == nullptr) {
                error(user_context) << "Vulkan: Failed to map host pointer to device memory!\n";
                return halide_error_code_copy_to_device_failed;
            }

            // copy to the (host-visible/coherent) staging buffer, then restore the dst pointer
            uint64_t copy_dst_ptr = copy_helper.dst;
            copy_helper.dst = (uint64_t)(stage_host_ptr);
            copy_memory(copy_helper, user_context);
            copy_helper.dst = copy_dst_ptr;

            // unmap the pointer
            error_code = ctx.allocator->unmap(user_context, staging.region);
            if (error_code != halide_error_code_success) {
                error(user_context) << "Vulkan: Failed to unmap staging region!\n";
                return halide_error_code_copy_to_device_failed;
            }

            // use the staging region and buffer from the copy source
            src_buffer_region = staging.region;
            dst_buffer_region = reinterpret_cast<MemoryRegion *>(dst->device);

        } else if (from_host && to_host) {
            debug(user_context) << " buffer copy from: host to: host\n";
            copy_memory(copy_helper, user_context);
            return halide_error_code_success;
        }

        if (src_buffer_region == nullptr) {
            error(user_context) << "Vulkan: Failed to retrieve source buffer for device memory!\n";
            return halide_error_code_internal_error;
        }

        if (dst_buffer_region == nullptr) {
            error(user_context) << "Vulkan: Failed to retrieve destination buffer for device memory!\n";
            return halide_error_code_internal_error;
        }

        // get the owning memory region (that holds the allocation)
        MemoryRegion *src_memory_region = ctx.allocator->owner_of(user_context, src_buffer_region);
        MemoryRegion *dst_memory_region = ctx.allocator->owner_of(user_context, dst_buffer_region);

        // retrieve the buffers from the owning allocation region
        VkBuffer *src_device_buffer = reinterpret_cast<VkBuffer *>(src_memory_region->handle);
        VkBuffer *dst_device_buffer = reinterpret_cast<VkBuffer *>(dst_memory_region->handle);

        VulkanSubmission *submission = vk_begin_submission(user_context, ctx.allocator, ctx.queue, ctx.queue_family_index);
        if (submission == nullptr) {
            error(user_context) << "Vulkan: Failed to begin a submission!\n";
            if (to_host) {
                return halide_error_code_copy_to_host_failed;
            } else {
                return halide_error_code_copy_to_device_failed;
            }
        }

        // define the src and dst config
        uint64_t copy_dst = copy_helper.dst;
        uint64_t src_offset = copy_helper.src_begin + (src_buffer_region->indexing.offset * src->type.bytes());
        uint64_t dst_offset = copy_helper.dst_begin + (dst_buffer_region->indexing.offset * dst->type.bytes());

        copy_helper.src = (uint64_t)(src_device_buffer);
        copy_helper.dst = (uint64_t)(dst_device_buffer);

        debug(user_context) << " src region=" << (void *)src_memory_region << " buffer=" << (void *)src_device_buffer << " copy_offset=" << src_offset << "\n";
        debug(user_context) << " dst region=" << (void *)dst_memory_region << " buffer=" << (void *)dst_device_buffer << " copy_offset=" << dst_offset << "\n";

        // enqueue the copy operation, using the allocated buffers
        error_code = vk_do_multidimensional_copy(user_context, submission->command_buffer, copy_helper,
                                                 src_offset, dst_offset,
                                                 src->dimensions,
                                                 from_host, to_host);

        if (error_code != halide_error_code_success) {
            error(user_context) << "Vulkan: vk_do_multidimensional_copy failed!\n";
            return error_code;
        }

        //// submit the copy. An upload's staging buffer is released by the submission
        //// once it is done; a download waits for it.
        if (from_host && !to_host) {
            vk_attach_staging_region(submission, staging.take(), src->size_in_bytes());
        }
        error_code = vk_end_submission(user_context, ctx.allocator, submission, to_host);
        if ((error_code == halide_error_code_success) && to_host) {
            error_code = vk_wait_for_submissions(user_context, ctx.allocator);
        }
        if (error_code != halide_error_code_success) {
            error(user_context) << "Vulkan: Failed to submit the copy!\n";
            if (to_host) {
                return halide_error_code_copy_to_host_failed;
            } else {
                return halide_error_code_copy_to_device_failed;
            }
        }

        if (!from_host && to_host) {
            // map the staging region to a host ptr
            uint8_t *stage_host_ptr = (uint8_t *)ctx.allocator->map(user_context, staging.region);
            if (stage_host_ptr == nullptr) {
                error(user_context) << "Vulkan: Failed to map host pointer to device memory!\n";
                return halide_error_code_internal_error;
            }

            // copy to the (host-visible/coherent) staging buffer
            copy_helper.dst = copy_dst;
            copy_helper.src = (uint64_t)(stage_host_ptr);
            copy_memory(copy_helper, user_context);

            // unmap the pointer and reclaim the staging region
            error_code = ctx.allocator->unmap(user_context, staging.region);
            if (error_code != halide_error_code_success) {
                error(user_context) << "Vulkan: Failed to unmap pointer for staging region!\n";
                return error_code;
            }
        }

        if (staging.region) {
            error_code = vk_release_region(user_context, ctx.allocator, staging.take());
        }

        if (error_code != halide_error_code_success) {
            error(user_context) << "Vulkan: Failed to release staging region allocation!\n";
        }

#ifdef DEBUG_RUNTIME
        uint64_t t_after = halide_current_time_ns(user_context);
        debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
#endif
    }

    return error_code;
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
    if (ctx.error != halide_error_code_success) {
        error(user_context) << "Vulkan: Failed to acquire context!\n";
        return ctx.error;
    }

#ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
#endif

    // get the allocated region for the device
    MemoryRegion *device_region = reinterpret_cast<MemoryRegion *>(halide_buffer->device);
    if (device_region == nullptr) {
        error(user_context) << "Vulkan: Failed to retrieve device region for buffer!\n";
        return halide_error_code_internal_error;
    }

    int error_code = ctx.allocator->destroy_crop(user_context, device_region);
    if (error_code != halide_error_code_success) {
        error(user_context) << "Vulkan: Failed to destroy crop for device region!\n";
        return error_code;
    }

#ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
#endif

    return halide_error_code_success;
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
        << "state_ptr: " << state_ptr << ", "
        << "entry: " << entry_name << ", "
        << "blocks: " << blocksX << "x" << blocksY << "x" << blocksZ << ", "
        << "threads: " << threadsX << "x" << threadsY << "x" << threadsZ << ", "
        << "shmem: " << shared_mem_bytes << "\n";
#endif

    VulkanContext ctx(user_context);
    if (ctx.error != halide_error_code_success) {
        error(user_context) << "Vulkan: Failed to acquire context!\n";
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
    //    --- If shader module doesn't exist yet, then lookup invokes compile
    //    1a. Locate the correct entry point for the kernel (code modules may contain multiple entry points)
    // 2. If the rest of the cache entry is uninitialized, then create new objects:
    //    2a. Create a descriptor set layout
    //    2b. Create a pipeline layout
    //    2c. Create a compute pipeline
    //    --- Apply specializations to pipeline for shared memory or workgroup sizes
    //    2d. Create a descriptor set
    //    --- The above can be cached between invocations ---
    // 3. Set bindings for buffers and args in the descriptor set
    //    3a. Create the buffer for the scalar params
    //    3b. Copy args into uniform buffer
    //    3c. Update buffer bindings for descriptor set
    // 4. Create a command buffer from the command pool
    // 5. Fill the command buffer with a dispatch call
    //    7a. Bind the compute pipeline
    //    7b. Bind the descriptor set
    //    7c. Add a dispatch to the command buffer
    //    7d. End the command buffer
    // 6. Submit the command buffer to our command queue
    // --- The following isn't the most efficient, but it's what we do in Metal etc. ---
    // 7. Wait until the queue is done with the command buffer
    // 8. Cleanup all temporary objects

    // 1. Get the shader module cache entry
    VulkanCompilationCacheEntry *cache_entry = nullptr;
    bool found = compilation_cache.lookup(ctx.device, state_ptr, cache_entry);
    if (!found || (cache_entry == nullptr)) {
        error(user_context) << "Vulkan: Failed to locate shader module! Unable to proceed!\n";
        return halide_error_code_internal_error;
    }

    // 1a. Locate the correct entry point from the cache
    bool found_entry_point = false;
    uint32_t entry_point_index = 0;
    VulkanCompiledShaderModule *shader_module = nullptr;
    for (uint32_t m = 0; m < cache_entry->module_count; m++) {
        VulkanCompiledShaderModule *compiled_shader = cache_entry->compiled_modules[m];
        for (uint32_t n = 0; (n < compiled_shader->shader_count) && !found_entry_point; ++n) {
            if (strcmp(compiled_shader->shader_bindings[n].entry_point_name, entry_name) == 0) {
                shader_module = compiled_shader;
                entry_point_index = n;
                found_entry_point = true;
            }
        }
    }

    if (!found_entry_point || (entry_point_index >= shader_module->shader_count) || (shader_module == nullptr)) {
        error(user_context) << "Vulkan: Failed to locate shader entry point! Unable to proceed!\n";
        return halide_error_code_internal_error;
    }

    debug(user_context) << " found entry point ["
                        << (entry_point_index + 1) << " of " << shader_module->shader_count
                        << "] '" << entry_name << "'\n";

    // 2. Create objects for execution
    if (shader_module->descriptor_set_layouts == nullptr) {
        error(user_context) << "Vulkan: Missing descriptor set layouts! Unable to proceed!\n";
        return halide_error_code_internal_error;
    }

    int error_code = halide_error_code_success;
    if (shader_module->pipeline_layout == VK_NULL_HANDLE) {

        // 2a. Create all descriptor set layouts
        for (uint32_t n = 0; n < shader_module->shader_count; ++n) {
            if (((void *)shader_module->descriptor_set_layouts[n]) == VK_NULL_HANDLE) {
                uint32_t uniform_buffer_count = shader_module->shader_bindings[n].uniform_buffer_count;
                uint32_t storage_buffer_count = shader_module->shader_bindings[n].storage_buffer_count;
                debug(user_context) << " creating descriptor set layout [" << n << "] " << shader_module->shader_bindings[n].entry_point_name << "\n";
                error_code = vk_create_descriptor_set_layout(user_context, ctx.allocator, uniform_buffer_count, storage_buffer_count, &(shader_module->descriptor_set_layouts[n]));
                if (error_code != halide_error_code_success) {
                    error(user_context) << "Vulkan: Failed to create descriptor set layout!\n";
                    return error_code;
                }
            }
        }

        // 2b. Create the pipeline layout
        error_code = vk_create_pipeline_layout(user_context, ctx.allocator, shader_module->shader_count, shader_module->descriptor_set_layouts, &(shader_module->pipeline_layout));
        if (error_code != halide_error_code_success) {
            error(user_context) << "Vulkan: Failed to create pipeline layout!\n";
            return error_code;
        }
    }

    VulkanDispatchData dispatch_data = {};
    dispatch_data.shared_mem_bytes = shared_mem_bytes;
    dispatch_data.global_size[0] = blocksX;
    dispatch_data.global_size[1] = blocksY;
    dispatch_data.global_size[2] = blocksZ;
    dispatch_data.local_size[0] = threadsX;
    dispatch_data.local_size[1] = threadsY;
    dispatch_data.local_size[2] = threadsZ;

    VulkanShaderBinding *entry_point_binding = (shader_module->shader_bindings + entry_point_index);

    // 2c. Setup the compute pipeline (eg override any specializations for shared mem or workgroup size)
    error_code = vk_setup_compute_pipeline(user_context, ctx.allocator, entry_point_binding, &dispatch_data, shader_module->shader_module, shader_module->pipeline_layout, &(entry_point_binding->compute_pipeline));
    if (error_code != halide_error_code_success) {
        error(user_context) << "Vulkan: Failed to setup compute pipeline!\n";
        return error_code;
    }

    // 3. Begin recording into a submission. Its descriptor sets and scalar argument
    //    buffers are its own, so that the work still in flight is left alone.
    VulkanSubmission *submission = vk_begin_submission(user_context, ctx.allocator, ctx.queue, ctx.queue_family_index,
                                                       entry_point_binding->storage_buffer_count);
    if (submission == nullptr) {
        error(user_context) << "Vulkan: Failed to begin a submission!\n";
        return halide_error_code_generic_error;
    }

    // 3a. Create a descriptor set
    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
    error_code = vk_create_submission_descriptor_set(user_context, ctx.allocator, submission,
                                                     shader_module->descriptor_set_layouts[entry_point_index],
                                                     entry_point_binding->storage_buffer_count,
                                                     &descriptor_set);
    if (error_code != halide_error_code_success) {
        error(user_context) << "Vulkan: Failed to create descriptor set!\n";
        return error_code;
    }

    // 3b. Copy the scalar parameters into a uniform buffer
    VkBuffer *args_buffer = nullptr;
    if (entry_point_binding->uniform_buffer_count) {
        size_t scalar_buffer_size = vk_estimate_scalar_uniform_buffer_size(user_context, arg_sizes, args, arg_is_buffer);
        if (scalar_buffer_size > 0) {
            MemoryRegion *args_region = vk_submission_args_region(user_context, ctx.allocator, submission, scalar_buffer_size);
            if (args_region == nullptr) {
                error(user_context) << "Vulkan: Failed to create scalar uniform buffer!\n";
                return halide_error_code_out_of_memory;
            }

            error_code = vk_update_scalar_uniform_buffer(user_context, ctx.allocator, args_region, arg_sizes, args, arg_is_buffer);
            if (error_code != halide_error_code_success) {
                error(user_context) << "Vulkan: Failed to update scalar uniform buffer!\n";
                return error_code;
            }

            args_buffer = reinterpret_cast<VkBuffer *>(args_region->handle);
            if (args_buffer == nullptr) {
                error(user_context) << "Vulkan: Failed to retrieve scalar args buffer for device memory!\n";
                return halide_error_code_internal_error;
            }
        }
    }

    // 3c. Update buffer bindings for descriptor set
    error_code = vk_update_descriptor_set(user_context, ctx.allocator, args_buffer, entry_point_binding->uniform_buffer_count, entry_point_binding->storage_buffer_count, arg_sizes, args, arg_is_buffer, descriptor_set);
    if (error_code != halide_error_code_success) {
        error(user_context) << "Vulkan: Failed to update descriptor set!\n";
        return error_code;
    }

    // 4. Fill the command buffer
    vk_record_dispatch_call(submission->command_buffer,
                            entry_point_binding->compute_pipeline,
                            shader_module->pipeline_layout,
                            descriptor_set,
                            entry_point_index,
                            blocksX, blocksY, blocksZ);

    // 5. End the dispatch. It is submitted together with the commands recorded
    //    around it, and nothing waits for it here: copies to the host and
    //    halide_vulkan_device_sync do.
    error_code = vk_end_submission(user_context, ctx.allocator, submission);
    if (error_code != halide_error_code_success) {
        error(user_context) << "Vulkan: Failed to submit command buffer!\n";
        return error_code;
    }

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
    return halide_error_code_success;
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
        error(user_context) << "Vulkan: Unable to wrap buffer ... invalid device pointer!\n";
        return halide_error_code_device_wrap_native_failed;
    }
    buf->device = vk_buffer;
    buf->device_interface = &vulkan_device_interface;
    buf->device_interface->impl->use_module();
    return halide_error_code_success;
}

WEAK int halide_vulkan_detach_vk_buffer(void *user_context, halide_buffer_t *buf) {
    if (buf->device == 0) {
        return halide_error_code_success;
    }
    if (buf->device_interface != &vulkan_device_interface) {
        error(user_context) << "Vulkan: Unable to detach buffer ... invalid device interface!\n";
        return halide_error_code_incompatible_device_interface;
    }
    buf->device = 0;
    buf->device_interface->impl->release_module();
    buf->device_interface = nullptr;
    return halide_error_code_success;
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
    if (ctx.error != halide_error_code_success) {
        error(user_context) << "Vulkan: Failed to acquire context!\n";
        return ctx.error;
    }

    // collect all unused allocations
    if (ctx.allocator) {
        ctx.allocator->collect(user_context);
    }
    return halide_error_code_success;
}

namespace {

WEAK __attribute__((constructor)) void register_vulkan_allocation_pool() {
    vulkan_allocation_pool.release_unused = &halide_vulkan_release_unused_device_allocations;
    halide_register_device_allocation_pool(&vulkan_allocation_pool);
}

WEAK __attribute__((destructor)) void halide_vulkan_cleanup() {
    // NOTE: In some cases, we've observed the NVIDIA driver causing a segfault
    //       at process exit.  It appears to be triggered by running multiple
    //       processes that use the Vulkan API, whereupon, one of the libs in
    //       their driver stack may crash inside the finalizer.  Unfortunately,
    //       any attempt to avoid it may also crash, since the function pointers
    //       obtained from the Vulkan loader appear to be invalid.
    //
    //       https://github.com/halide/Halide/issues/8497
    //
    //       So, we don't do any special handling here ... just clean up like
    //       the other runtimes do.
    //
    if (halide_vulkan_is_initialized()) {
        halide_vulkan_device_release(nullptr);
    }
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
    halide_vulkan_buffer_copy,
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
    halide_vulkan_compute_capability,
    &vulkan_device_interface_impl};

// --------------------------------------------------------------------------

}  // namespace Vulkan
}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide
