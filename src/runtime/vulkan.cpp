#include "HalideRuntimeVulkan.h"
#include "scoped_spin_lock.h"
#include "device_buffer_utils.h"
#include "device_interface.h"
#include "printer.h"

#define VK_NO_PROTOTYPES
#include "mini_vulkan.h"

#define INLINE inline __attribute__((always_inline))

namespace Halide { namespace Runtime { namespace Internal { namespace Vulkan {

#define VULKAN_FN(fn) WEAK PFN_##fn fn;
#include "vulkan_functions.h"
#undef VULKAN_FN

void WEAK load_vulkan_functions(VkInstance instance) {
    #define VULKAN_FN(fn) fn = (PFN_##fn)vkGetInstanceProcAddr(instance, #fn);
    #include "vulkan_functions.h"
    #undef VULKAN_FN
}

extern WEAK halide_device_interface_t vulkan_device_interface;

WEAK const char *get_vulkan_error_name(VkResult error);

// An Vulkan context/queue/synchronization lock defined in
// this module with weak linkage
VkInstance WEAK cached_instance = 0;
VkDevice WEAK cached_device = 0;
VkQueue WEAK cached_queue = 0;
volatile int WEAK thread_lock = 0;

}}}} // namespace Halide::Runtime::Internal::Vulkan

using namespace Halide::Runtime::Internal::Vulkan;

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
WEAK int halide_vulkan_acquire_context(void *user_context, VkInstance *instance,
                                       VkDevice *device, VkQueue *queue, bool create = true) {
    // TODO: Should we use a more "assertive" assert? These asserts do
    // not block execution on failure.
    halide_assert(user_context, instance != NULL);
    halide_assert(user_context, device != NULL);
    halide_assert(user_context, queue != NULL);

    if (cached_instance == NULL && create) {
        VkInstanceCreateInfo create_info = {
            VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            NULL,    // Next
            0,       // Flags
            NULL,    // ApplicationInfo
            0, NULL, // Layers
            0, NULL  // Extensions
        };
        VkResult ret_code = vkCreateInstance(&create_info, NULL, &cached_instance);
        if (ret_code != VK_SUCCESS) {
            // TODO: Get info on error and return approriate code.
            return -1;
        }

        if (vkCreateDevice == NULL) {
            load_vulkan_functions(cached_instance);
        }
        
        VkPhysicalDevice chosen_device = NULL;
        VkPhysicalDevice devices[16];
        uint32_t queue_family;
        uint32_t device_count = sizeof(devices) / sizeof(devices[0]);
        ret_code = vkEnumeratePhysicalDevices(cached_instance, &device_count, devices);
        // For now handle more than 16 devices by just looking at the first 16.
        halide_assert(user_context, ret_code == VK_SUCCESS || ret_code == VK_INCOMPLETE);

        if (device_count == 0) {
            debug(user_context) << "Vulkan: No devices found.\n";
            return -1;
        }
        // Try to find a device that supports compute.
        for (uint32_t i = 0; chosen_device == NULL && i < device_count; i++) {
            VkPhysicalDeviceProperties properties;
            vkGetPhysicalDeviceProperties(devices[i], &properties);

            if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU || VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
                VkQueueFamilyProperties queue_properties[16];
                uint32_t queue_properties_count = sizeof(queue_properties) / sizeof(queue_properties[0]);
                vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &queue_properties_count, queue_properties);
                halide_assert(user_context, ret_code == VK_SUCCESS || ret_code == VK_INCOMPLETE);
                for (uint32_t j = 0; chosen_device == NULL && j < queue_properties_count; j++) {
                    if (queue_properties[j].queueCount > 0 &&
                        queue_properties[j].queueFlags & VK_QUEUE_COMPUTE_BIT) {
                        chosen_device = devices[i];
                        queue_family = j;
                    }
                }
            }
        }
        // If nothing, just try the first one for now.
        if (chosen_device == NULL) {
            queue_family = 0;
            chosen_device = devices[0];
        }

        float queue_priority = 1.0f;
        VkDeviceQueueCreateInfo device_queue_create_info = {
            VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            NULL, // Next
            0,    // Flags
            queue_family,
            1,
            &queue_priority,
        };

        VkDeviceCreateInfo device_create_info = {
            VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            NULL, // Next
            0,    // Flags
            1,    // Count of queues to create
            &device_queue_create_info,
            0,    // Enabled layers
            NULL, // layer names
            0,    // Enabled extensions
            NULL, // Enabled extension names
            NULL, // VkPhysicalDeviceFeatures
        };

        ret_code = vkCreateDevice(chosen_device, &device_create_info, NULL, &cached_device);
        if (ret_code != VK_SUCCESS) {
          debug(user_context) << "Vulkan: vkCreateDevice failed with return code: " << get_vulkan_error_name(ret_code) << "\n";
          return -1;
        }

        vkGetDeviceQueue(cached_device, queue_family, 0, &cached_queue);
    }

    *instance = cached_instance;
    *device = cached_device;
    *queue = cached_queue;

    return 0;
}

WEAK int halide_vulkan_release_context(void *user_context, VkInstance instance, VkDevice device, VkQueue queue) {
    return 0;
}

} // extern "C"

namespace Halide { namespace Runtime { namespace Internal { namespace Vulkan {

// Helper object to acquire and release the Vulkan context.
class VulkanContext {
    void *user_context;

public:
    VkInstance instance;
    VkDevice device;
    VkQueue queue;
    VkResult error;

    INLINE VulkanContext(void *user_context) : user_context(user_context),
                                             instance(NULL), device(NULL), queue(NULL),
                                             error(VK_SUCCESS) {
        
        while (__sync_lock_test_and_set(&thread_lock, 1)) { }

        int err_halide = halide_vulkan_acquire_context(user_context, &instance, &device, &queue);
        halide_assert(user_context, err_halide == 0);
        halide_assert(user_context, device != NULL && queue != NULL);

        __sync_lock_release(&thread_lock);
    }

    INLINE ~VulkanContext() {
        halide_vulkan_release_context(user_context, instance, device, queue);
    }

    // For now, this is always NULL
    INLINE const VkAllocationCallbacks *allocation_callbacks() { return NULL; }
};

// Structure to hold the state of a module attached to the context.
// Also used as a linked-list to keep track of all the different
// modules that are attached to a context in order to release them all
// when then context is released.
struct module_state {
    // TODO: Could also be a VkShaderModule
    VkPipeline pipeline;
    module_state *next;
};
WEAK module_state *state_list = NULL;

}}}} // namespace Halide::Runtime::Internal::Vulkan

extern "C" {

WEAK int halide_vulkan_device_free(void *user_context, halide_buffer_t* buf) {
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
    buf->device_interface = NULL;

    #ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
    #endif

    return 0;
}


WEAK int halide_vulkan_initialize_kernels(void *user_context, void **state_ptr, const char* src, int size) {
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

    // Create the state object if necessary. This only happens once, regardless
    // of how many times halide_init_kernels/halide_release is called.
    // halide_release traverses this list and releases the program objects, but
    // it does not modify the list nodes created/inserted here.
    module_state **state = (module_state**)state_ptr;
    if (!(*state)) {
        *state = (module_state*)malloc(sizeof(module_state));
        (*state)->pipeline = NULL;
        (*state)->next = state_list;
        state_list = *state;
    }

    // Create the program if necessary. TODO: The program object needs to not
    // only already exist, but be created for the same context/device as the
    // calling context/device.
    if (!(*state && (*state)->pipeline) && size > 1) {
    }

    #ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
    #endif

    return 0;
}

// Used to generate correct timings when tracing
WEAK int halide_vulkan_device_sync(void *user_context, halide_buffer_t *) {
    debug(user_context) << "Vulkan: halide_vulkan_device_sync (user_context: " << user_context << ")\n";

    VulkanContext ctx(user_context);
    halide_assert(user_context, ctx.error == VK_SUCCESS);

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

    int err;
    VkInstance instance;
    VkDevice device;
    VkQueue queue;
    err = halide_vulkan_acquire_context(user_context, &instance, &device, &queue, false);
    if (instance != NULL) {
        // SYNC

        // Unload the modules attached to this context. Note that the list
        // nodes themselves are not freed, only the program objects are
        // released. Subsequent calls to halide_init_kernels might re-create
        // the program object using the same list node to store the program
        // object.
        module_state *state = state_list;
        while (state) {
            if (state->pipeline) {

                debug(user_context) << "    vkDestroyPipeline " << state->pipeline << "\n";
                vkDestroyPipeline(device, state->pipeline, NULL /* TODO: alloc callbacks. */);
                state->pipeline = NULL;
            }
            state = state->next;
        }
        
        halide_vulkan_release_context(user_context, instance, device, queue);
        vkDestroyDevice(device, NULL);
        if (instance == cached_instance) {
            cached_instance = NULL;
        }
        vkDestroyInstance(instance, NULL);
    }

    return 0;
}

WEAK int halide_vulkan_device_malloc(void *user_context, halide_buffer_t* buf) {
    debug(user_context)
        << "halide_vulkan_device_malloc (user_context: " << user_context
        << ", buf: " << buf << ")\n";

    VulkanContext context(user_context);
    if (context.error != VK_SUCCESS) {
        return -1;
    }

    size_t size = buf->size_in_bytes();
    halide_assert(user_context, size != 0);
    if (buf->device) {
        return 0;
    }

    for (int i = 0; i < buf->dimensions; i++) {
        halide_assert(user_context, buf->dim[i].stride >= 0);
    }

    debug(user_context) << "    allocating " << *buf << "\n";

    #ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
    #endif

    VkBufferCreateInfo args_info = {
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, NULL,
        0,
        size,
        // TODO: verify next flags
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_SHARING_MODE_EXCLUSIVE,
        0, NULL
    };
    VkBuffer result;
    VkResult ret_code = vkCreateBuffer(context.device, &args_info, NULL, &result);
    if (ret_code != VK_SUCCESS) {
        debug(user_context) << "Vulkan: vkCreateBuffer returned: " << get_vulkan_error_name(ret_code) << "\n";
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
                           VkCommandBuffer                             commandBuffer,
                           VkBuffer                                    srcBuffer,
                           VkBuffer                                    dstBuffer,
                           uint32_t                                    regionCount,
                           const VkBufferCopy*                         pRegions);

    } else if (d == 2) {
    } else {
        for (int i = 0; i < (int)c.extent[d-1]; i++) {
            int err = do_multidimensional_copy(user_context, ctx, c, off, d-1, d_to_h);
            off += c.src_stride_bytes[d-1];
            if (err) {
                return err;
            }
        }
    }
    return 0;
}
}

WEAK int halide_vulkan_copy_to_device(void *user_context, halide_buffer_t* buf) {
    int err = halide_vulkan_device_malloc(user_context, buf);
    if (err) {
        return err;
    }

    debug(user_context)
        << "Vulkan: halide_vulkan_copy_to_device (user_context: " << user_context
        << ", buf: " << buf << ")\n";

    // Acquire the context so we can use the command queue. This also avoids multiple
    // redundant calls to clEnqueueWriteBuffer when multiple threads are trying to copy
    // the same buffer.
    VulkanContext ctx(user_context);
    if (ctx.error != VK_SUCCESS) {
        return ctx.error;
    }

    #ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
    #endif

    halide_assert(user_context, buf->host && buf->device);

    device_copy c = make_host_to_device_copy(buf);

    do_multidimensional_copy(user_context, ctx, c, 0, buf->dimensions, false);

    // TODO: sync

    #ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
    #endif

    return 0;
}

WEAK int halide_vulkan_copy_to_host(void *user_context, halide_buffer_t* buf) {
    debug(user_context)
        << "Vulkan: halide_copy_to_host (user_context: " << user_context
        << ", buf: " << buf << ")\n";

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

    halide_assert(user_context, buf->host && buf->device);

    device_copy c = make_device_to_host_copy(buf);

    do_multidimensional_copy(user_context, ctx, c, 0, buf->dimensions, true);

    // TODO: sync

    #ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
    #endif

    return 0;
}

WEAK int halide_vulkan_run(void *user_context,
                           void *state_ptr,
                           const char* entry_name,
                           int blocksX, int blocksY, int blocksZ,
                           int threadsX, int threadsY, int threadsZ,
                           int shared_mem_bytes,
                           size_t arg_sizes[],
                           void* args[],
                           int8_t arg_is_buffer[],
                           int num_attributes,
                           float* vertex_buffer,
                           int num_coords_dim0,
                           int num_coords_dim1) {
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
    halide_assert(user_context, buf->device == 0);
    if (buf->device != 0) {
        return -2;
    }
    buf->device = vk_buffer;
    buf->device_interface = &vulkan_device_interface;
    buf->device_interface->impl->use_module();

    return 0;
}

WEAK int halide_vulkan_detach_vk_buffer(void *user_context, halide_buffer_t *buf) {
    if (buf->device == NULL) {
        return 0;
    }
    halide_assert(user_context, buf->device_interface == &vulkan_device_interface);
    buf->device = 0;
    buf->device_interface->impl->release_module();
    buf->device_interface = NULL;
    return 0;
}

WEAK uintptr_t halide_vulkan_get_vk_buffer(void *user_context, halide_buffer_t *buf) {
    if (buf->device == NULL) {
        return 0;
    }
    halide_assert(user_context, buf->device_interface == &vulkan_device_interface);
    return (uintptr_t)buf->device;
}

WEAK const struct halide_device_interface_t *halide_vulkan_device_interface() {
    return &vulkan_device_interface;
}

namespace {
__attribute__((destructor))
WEAK void halide_vulkan_cleanup() {
    halide_vulkan_device_release(NULL);
}
}

} // extern "C" linkage

namespace Halide { namespace Runtime { namespace Internal { namespace Vulkan {

WEAK const char *get_vulkan_error_name(VkResult err) {
    switch (err) {
        case VK_SUCCESS: return "VK_SUCCESS";
        case VK_NOT_READY: return "VK_NOT_READY";
        case VK_TIMEOUT: return "VK_TIMEOUT";
        case VK_EVENT_SET: return "VK_EVENT_SET";
        case VK_EVENT_RESET: return "VK_EVENT_RESET";
        case VK_INCOMPLETE: return "VK_INCOMPLETE";
        case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
        case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_TOO_MANY_OBJECTS: return "VK_ERROR_TOO_MANY_OBJECTS";
        case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
        case VK_ERROR_FRAGMENTED_POOL: return "VK_ERROR_FRAGMENTED_POOL";
        case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
        case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR: return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
        case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
        case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
        case VK_ERROR_INCOMPATIBLE_DISPLAY_KHR: return "VK_ERROR_INCOMPATIBLE_DISPLAY_KHR";
        case VK_ERROR_VALIDATION_FAILED_EXT: return "VK_ERROR_VALIDATION_FAILED_EXT";
        case VK_ERROR_INVALID_SHADER_NV: return "VK_ERROR_INVALID_SHADER_NV";
        case VK_ERROR_OUT_OF_POOL_MEMORY_KHR: return "VK_ERROR_OUT_OF_POOL_MEMORY_KHR";
        case VK_ERROR_INVALID_EXTERNAL_HANDLE_KHR: return "VK_ERROR_INVALID_EXTERNAL_HANDLE_KHR";
        default: return "<Unknown Vulkan Result Code>";
    }
}

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
    NULL, // target capabilities.
    &vulkan_device_interface_impl
};

}}}} // namespace Halide::Runtime::Internal::Vulkan
