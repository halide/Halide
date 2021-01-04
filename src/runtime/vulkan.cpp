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
uint32_t WEAK cached_memory_type_index = 0;
uint32_t WEAK cached_queue_family_index = 0;
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
                                       VkDevice *device, VkQueue *queue, uint32_t* memory_type_index, uint32_t* queue_family_index, bool create) {
    // TODO: Should we use a more "assertive" assert? These asserts do
    // not block execution on failure.
    halide_assert(user_context, instance != nullptr);
    halide_assert(user_context, device != nullptr);
    halide_assert(user_context, queue != nullptr);

    // TODO: make validation optional & only in debug

    const char* val_layers[] = {"VK_LAYER_KHRONOS_validation"};
#define VK_MAKE_VERSION(major, minor, patch) \
    ((((uint32_t)(major)) << 22) | (((uint32_t)(minor)) << 12) | ((uint32_t)(patch)))

    if (cached_instance == nullptr && create) {
        VkApplicationInfo app_info = {
            VK_STRUCTURE_TYPE_APPLICATION_INFO, // struct type
            nullptr, // Next
            nullptr, // application name
            0, // app version
            "Halide", // engine name
            0, // engine version
            VK_MAKE_VERSION(1, 2, 0)
        };
        VkInstanceCreateInfo create_info = {
            VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            nullptr,    // Next
            0,       // Flags
            &app_info,    // ApplicationInfo
//            0, nullptr, // Layers
            1, val_layers,
            0, nullptr  // Extensions
        };
        VkResult ret_code = vkCreateInstance(&create_info, nullptr, &cached_instance);
        if (ret_code != VK_SUCCESS) {
            // TODO: Get info on error and return approriate code.
            return -1;
        }

        if (vkCreateDevice == nullptr) {
            load_vulkan_functions(cached_instance);
        }
        
        VkPhysicalDevice chosen_device = nullptr;
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
        for (uint32_t i = 0; chosen_device == nullptr && i < device_count; i++) {
            VkPhysicalDeviceProperties properties;
            vkGetPhysicalDeviceProperties(devices[i], &properties);

            if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU || 
                properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
                VkQueueFamilyProperties queue_properties[16];
                uint32_t queue_properties_count = sizeof(queue_properties) / sizeof(queue_properties[0]);
                vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &queue_properties_count, queue_properties);
                halide_assert(user_context, ret_code == VK_SUCCESS || ret_code == VK_INCOMPLETE);
                for (uint32_t j = 0; chosen_device == nullptr && j < queue_properties_count; j++) {
                    if (queue_properties[j].queueCount > 0 &&
                        queue_properties[j].queueFlags & VK_QUEUE_COMPUTE_BIT) {
                        chosen_device = devices[i];
                        queue_family = j;
                    }
                }
            }
        }
        // If nothing, just try the first one for now.
        if (chosen_device == nullptr) {
            queue_family = 0;
            chosen_device = devices[0];
        }

        float queue_priority = 1.0f;
        VkDeviceQueueCreateInfo device_queue_create_info = {
            VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            nullptr, // Next
            0,    // Flags
            queue_family,
            1,
            &queue_priority,
        };

        VkDeviceCreateInfo device_create_info = {
            VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            nullptr, // Next
            0,    // Flags
            1,    // Count of queues to create
            &device_queue_create_info,
            0,    // Enabled layers
            nullptr, // layer names
            0,    // Enabled extensions
            nullptr, // Enabled extension names
            nullptr, // VkPhysicalDeviceFeatures
        };

        ret_code = vkCreateDevice(chosen_device, &device_create_info, nullptr, &cached_device);
        if (ret_code != VK_SUCCESS) {
          debug(user_context) << "Vulkan: vkCreateDevice failed with return code: " << get_vulkan_error_name(ret_code) << "\n";
          return -1;
        }

        vkGetDeviceQueue(cached_device, queue_family, 0, &cached_queue);

        cached_queue_family_index = queue_family;

        // Find an appropriate memory type for allocating buffers
        cached_memory_type_index = VK_MAX_MEMORY_TYPES;
        VkPhysicalDeviceMemoryProperties device_mem_properties;
        vkGetPhysicalDeviceMemoryProperties(chosen_device, &device_mem_properties);

        // TODO: should this be host coherent or cached or something else?
        for (uint32_t i = 0; i < device_mem_properties.memoryTypeCount; i++) {
            if ((device_mem_properties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) &&
                (device_mem_properties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
                    cached_memory_type_index = i;
                    break;
                }
        }
        if (cached_memory_type_index == VK_MAX_MEMORY_TYPES) {
            debug(user_context) << "Vulkan: unable to find appropriate memory type\n";
            return -1;
        }
    }

    *instance = cached_instance;
    *device = cached_device;
    *queue = cached_queue;
    *memory_type_index = cached_memory_type_index;
    *queue_family_index = cached_queue_family_index;

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
    uint32_t memory_type_index; // used for choosing which memory type to use
    uint32_t queue_family_index; // used for operations requiring queue family

    INLINE VulkanContext(void *user_context) : user_context(user_context),
                                             instance(nullptr), device(nullptr), queue(nullptr),
                                             error(VK_SUCCESS), memory_type_index(0),
                                             queue_family_index(0) {
        
        while (__sync_lock_test_and_set(&thread_lock, 1)) { }

        int err_halide = halide_vulkan_acquire_context(user_context, &instance, &device, &queue, &memory_type_index, &queue_family_index);
        halide_assert(user_context, err_halide == 0);
        halide_assert(user_context, device != nullptr && queue != nullptr);

        __sync_lock_release(&thread_lock);
    }

    INLINE ~VulkanContext() {
        halide_vulkan_release_context(user_context, instance, device, queue);
    }

    // For now, this is always nullptr
    INLINE const VkAllocationCallbacks *allocation_callbacks() { return nullptr; }
};

// Structure to hold the state of a module attached to the context.
// Also used as a linked-list to keep track of all the different
// modules that are attached to a context in order to release them all
// when then context is released.
struct module_state {
    // TODO: Could also be a VkPipeline, but in that case we need to
    // pass information required to construct the VkDescriptorSetLayout
    // to halide_vulkan_initialize_kernels().  This will require modifying
    // Vulkan GPU codegen to pass in the required info
    // So, for now, we'll use a VKShaderModule here and do the boilerplate
    // of creating a pipeline in the run function.
    VkShaderModule shader_module;
    module_state *next;
};
WEAK module_state *state_list = nullptr;

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
    buf->device_interface = nullptr;

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
        (*state)->shader_module = 0;
        (*state)->next = state_list;
        state_list = *state;
    }

    // Create the program if necessary. TODO: The program object needs to not
    // only already exist, but be created for the same context/device as the
    // calling context/device.
    if (!(*state && (*state)->shader_module) && size > 1) {
        VkShaderModuleCreateInfo shader_info = {
            VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            nullptr,        // pointer to structure extending this
            0,              // flags (curently unused)
            (size_t)size,   // code size in bytes
            (const uint32_t*)src  // source
        };

        debug(user_context) << "    vkCreateShaderModule src: " << (const uint32_t*)src << "\n";

        VkResult ret_code = vkCreateShaderModule(ctx.device, &shader_info, ctx.allocation_callbacks(), &((*state)->shader_module));
        if (ret_code != VK_SUCCESS) {
            debug(user_context) << "Vulkan: vkCreateShaderModule returned: " << get_vulkan_error_name(ret_code) << "\n";
            return -1;
        }
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
    uint32_t _throwaway;
    err = halide_vulkan_acquire_context(user_context, &instance, &device, &queue, &_throwaway, &_throwaway, false);
    if (instance != nullptr) {
        // SYNC

        // Unload the modules attached to this context. Note that the list
        // nodes themselves are not freed, only the program objects are
        // released. Subsequent calls to halide_init_kernels might re-create
        // the program object using the same list node to store the program
        // object.
        module_state *state = state_list;
        while (state) {
            if (state->shader_module) {

                debug(user_context) << "    vkDestroyShaderModule " << state->shader_module << "\n";
                vkDestroyShaderModule(device, state->shader_module, nullptr /* TODO: alloc callbacks. */);
                state->shader_module = 0;
            }
            state = state->next;
        }
        
        halide_vulkan_release_context(user_context, instance, device, queue);
        vkDestroyDevice(device, nullptr);
        if (instance == cached_instance) {
            cached_instance = nullptr;
        }
        vkDestroyInstance(instance, nullptr);
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

    // In Vulkan, we need to go through the following steps in order
    // to set up a device allocation for a Halide buffer:
    // 1. Allocate memory that backs the buffer.  This needs to be
    //    the appropriate memory type with the appropriate properties
    // 2. Construct a VkBuffer
    // 3. Bind the VkBuffer to the allocated memory
    // TODO: This can greatly benefit from, and is designed for, an
    // allocation cache.  We should consider allocating larger chunks
    // of memory and using the larger allocation (with appropriate size/offsets)
    // to back buffers created here

    // Allocate the memory.  We rely on the halide_vulkan_acquire_context() API to
    // tell us which memory type to use
    VkMemoryAllocateInfo alloc_info =
        {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, // struct type
         nullptr, // struct extending this
         size,  // size of allocation in bytes
         context.memory_type_index  // memory type index from physical device
        };
    VkDeviceMemory device_memory;
    auto ret_code = vkAllocateMemory(context.device, &alloc_info, context.allocation_callbacks(), &device_memory);
    if (ret_code != VK_SUCCESS) {
        debug(user_context) << "Vulkan: vkAllocateMemory returned: " << get_vulkan_error_name(ret_code) << "\n";
        return -1;
    }

    // Now create the buffer

    VkBufferCreateInfo args_info = {
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr,
        0,
        size,
        // TODO: verify next flags
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_SHARING_MODE_EXCLUSIVE,
        0, nullptr
    };
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
    const size_t HALIDE_MAX_VK_BINDINGS=64;
    VkDescriptorSetLayoutBinding layout_bindings[HALIDE_MAX_VK_BINDINGS];

    // The first binding is used for scalar parameters
    uint32_t num_bindings = 1;
    layout_bindings[0] = {0, // binding
                          VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, // descriptor type
                          1, // descriptor count
                          VK_SHADER_STAGE_COMPUTE_BIT, // stage flags
                          0}; // immutable samplers

    int i = 0;
    int scalar_buffer_size = 0;
    while (arg_sizes[i] > 0) {
        if (arg_is_buffer[i]) {
            // TODO: I don't quite understand why STORAGE_BUFFER is valid
            // here, but examples all across the docs seem to do this
            layout_bindings[num_bindings] = 
                        {num_bindings, // binding
                         VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, // descriptor type
                         1, // descriptor count
                         VK_SHADER_STAGE_COMPUTE_BIT, // stage flags
                         0}; // immutable samplers
            num_bindings++;
        } else {
            scalar_buffer_size += arg_sizes[i];
        }
        i++;
    }
    // Create the LayoutInfo struct
    VkDescriptorSetLayoutCreateInfo layout_info = 
        {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,  // structure type
         nullptr, // pointer to a struct extending this info
         0, // flags
         num_bindings, // binding count
         layout_bindings // pointer to layout bindings array
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
    VkMemoryAllocateInfo scalar_alloc_info = 
        {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,    // struct type
         nullptr,   // point to struct extending this
         (uint32_t)scalar_buffer_size,    // allocation size
         ctx.memory_type_index  // memory type
        };
    VkDeviceMemory scalar_alloc;
    result = vkAllocateMemory(ctx.device, &scalar_alloc_info, 0, &scalar_alloc); 
    if (result != VK_SUCCESS) {
        debug(user_context) << "vkAllocateMemory returned " << get_vulkan_error_name(result) << "\n";
        return result;
    }

    uint8_t* scalar_ptr;
    result = vkMapMemory(ctx.device, scalar_alloc, 0, scalar_buffer_size, 0, (void**)&scalar_ptr);

    if (result != VK_SUCCESS) {
        debug(user_context) << "vkMapMemory returned " << get_vulkan_error_name(result) << "\n";
        return result;
    }

    size_t scalar_arg_offset = 0;
    for (size_t i = 0; arg_sizes[i] > 0; i++) {
        if (!arg_is_buffer[i]) {
            memcpy(scalar_ptr+scalar_arg_offset, args + i, arg_sizes[i]); 
            scalar_arg_offset += arg_sizes[i];
        }
    }
    vkUnmapMemory(ctx.device, scalar_alloc);

    VkBufferCreateInfo scalar_buffer_info =
        {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,  // struct type
         nullptr,   // point to struct extending this
         0, // flags
         (VkDeviceSize)scalar_buffer_size,    // size
         VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, // usages
         VK_SHARING_MODE_EXCLUSIVE,     // sharing across queues
         0,     // irrelevant here
         nullptr // irrelevant
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
        {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, // structure type
         nullptr, // pointer to a structure extending this
         0, // flags
         1, // number of descriptor sets
         &descriptor_set_layout, // pointer to the descriptor sets
         0, // number of push constant ranges
         nullptr // pointer to push constant range structs
        };
    
    VkPipelineLayout pipeline_layout;
    result = vkCreatePipelineLayout(ctx.device, &pipeline_layout_info, 0, &pipeline_layout);

    if (result != VK_SUCCESS) {
        debug(user_context) << "vkCreatePipelineLayout returned " << get_vulkan_error_name(result) << "\n";
        return result;
    }

    //// 3. Create a compute pipeline
    // Get the shader module
    halide_assert(user_context, state_ptr);
    module_state *state = (module_state *)state_ptr;

    VkComputePipelineCreateInfo compute_pipeline_info = 
        {VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, // structure type
         nullptr, // pointer to a structure extending this
         0, // flags
         // VkPipelineShaderStageCreatInfo
         {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, // structure type
          nullptr, //pointer to a structure extending this 
          0, // flags
          VK_SHADER_STAGE_COMPUTE_BIT, // compute stage shader
          state->shader_module, // shader module
          entry_name, // entry point name
          nullptr // pointer to VkSpecializationInfo struct
         },
         pipeline_layout, // pipeline layout
         0, // base pipeline handle for derived pipeline
         0  // base pipeline index for derived pipeline
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
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,      // descriptor type
            1                                       // how many
        },
        {
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,      // descriptor type
            num_bindings - 1                        // how many
        }
    };

    VkDescriptorPoolCreateInfo descriptor_pool_info  = 
        {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, // struct type
         nullptr, // point to struct extending this
         0, // flags
         num_bindings, // max numbewr of sets that can be allocated TODO:should this be 1?
         2, // pool size count
         descriptor_pool_sizes // ptr to descriptr pool sizes
        };

    VkDescriptorPool descriptor_pool;
    result = vkCreateDescriptorPool(ctx.device, &descriptor_pool_info, 0, &descriptor_pool);

    if (result != VK_SUCCESS) {
        debug(user_context) << "vkCreateDescriptorPool returned " << get_vulkan_error_name(result) << "\n";
        return result;
    }

    VkDescriptorSetAllocateInfo descriptor_set_info =
        {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,    // struct type
         nullptr,   // pointer to struct extending this
         descriptor_pool,  // pool from which to allocate sets
         1,     // number of descriptor sets
         &descriptor_set_layout // pointer to array of descriptor set layouts
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
        {scalar_args_buffer,    // the buffer
         0,     // offset
         VK_WHOLE_SIZE  // range
        };
    write_descriptor_set[0] =
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,    // struct type
         nullptr,   // pointer to struct extending this
         descriptor_set,    // descriptor set to update
         0,     // binding
         0,     // array elem
         1,     // num to update
         VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, // descriptor type
         nullptr,   // for images
         &(descriptor_buffer_info[0]),  // info for buffer
         nullptr    // for texel buffers
        };
    uint32_t num_bound = 1;
    for (size_t i = 0; arg_sizes[i] > 0; i++) {
        if (arg_is_buffer[i]) {
            halide_assert(user_context, num_bound < HALIDE_MAX_VK_BINDINGS);
            auto buf = (VkBuffer)( ((halide_buffer_t *)args[i])->device );
            descriptor_buffer_info[num_bound] =
                {buf,    // the buffer
                0,     // offset
                VK_WHOLE_SIZE  // range
                };
            write_descriptor_set[num_bound] =
                {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,    // struct type
                nullptr,   // pointer to struct extending this
                descriptor_set,    // descriptor set to update
                num_bound,     // binding
                0,     // array elem
                1,     // num to update
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, // descriptor type
                nullptr,   // for images
                &(descriptor_buffer_info[num_bound]),  // info for buffer
                nullptr    // for texel buffers
                };
            num_bound++;
        }
    }

    halide_assert(user_context, num_bound == num_bindings);
    vkUpdateDescriptorSets(ctx.device, num_bindings, write_descriptor_set, 0, nullptr);
    
    //// 6. Create a command pool
    // TODO: This should really be part of the acquire_context API
    VkCommandPoolCreateInfo command_pool_info =
        {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,    // struct type
         nullptr,   // pointer to struct extending this
         0,     // flags.  may consider VK_COMMAND_POOL_CREATE_TRANSIENT_BIT
         ctx.queue_family_index     // queue family index corresponding to the compute command queue
        };
    VkCommandPool command_pool;
    result = vkCreateCommandPool(ctx.device, &command_pool_info, ctx.allocation_callbacks(), &command_pool);

    if (result != VK_SUCCESS) {
        debug(user_context) << "vkCreateCommandPool returned " << get_vulkan_error_name(result) << "\n";
        return result;
    }

    //// 7. Create a command buffer from the command pool
    VkCommandBufferAllocateInfo command_buffer_info =
        {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,    // struct type
         nullptr,    // pointer to struct extending this
         command_pool,  // command pool for allocation
         VK_COMMAND_BUFFER_LEVEL_PRIMARY,   // command buffer level
         1  // number to allocate
        };
    
    VkCommandBuffer command_buffer;
    result = vkAllocateCommandBuffers(ctx.device, &command_buffer_info, &command_buffer);

    if (result != VK_SUCCESS) {
        debug(user_context) << "vkAllocateCommandBuffer returned " << get_vulkan_error_name(result) << "\n";
        return result;
    }

    //// 8. Begin the command buffer
    VkCommandBufferBeginInfo command_buffer_begin_info =
        {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,   // struct type
         nullptr,   // pointer to struct extending this
         VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, // flags
         nullptr    // pointer to parent command buffer
        };
 
    result = vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info);

    if (result != VK_SUCCESS) {
        debug(user_context) << "vkAllocateCommandBuffer returned " << get_vulkan_error_name(result) << "\n";
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
        {VK_STRUCTURE_TYPE_SUBMIT_INFO,     // struct type
         nullptr,       // pointer to struct extending this
         0,             // wait semaphore count
         nullptr,       // semaphores
         nullptr,       // pipeline stages where semaphore waits occur
         1,             // how many command buffers to execute
         &command_buffer,   // the command buffers
         0,             // number of semaphores to signal
         nullptr        // the semaphores to signal
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
    if (buf->device == 0) {
        return 0;
    }
    halide_assert(user_context, buf->device_interface == &vulkan_device_interface);
    buf->device = 0;
    buf->device_interface->impl->release_module();
    buf->device_interface = nullptr;
    return 0;
}

WEAK uintptr_t halide_vulkan_get_vk_buffer(void *user_context, halide_buffer_t *buf) {
    if (buf->device == 0) {
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
    halide_vulkan_device_release(nullptr);
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
    nullptr, // target capabilities.
    &vulkan_device_interface_impl
};

}}}} // namespace Halide::Runtime::Internal::Vulkan
