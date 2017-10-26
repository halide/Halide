#include "HalideRuntimeVulkan.h"
#include "scoped_spin_lock.h"
#include "device_buffer_utils.h"
#include "device_interface.h"
#include "printer.h"

#include "mini_vulkan.h"

/* List of Vulkan functions used:
    vkCreateBuffer
    vkDestroyBuffer
    vkCreateInstance
    vkDestroyInstance

*/

#define INLINE inline __attribute__((always_inline))

namespace Halide { namespace Runtime { namespace Internal { namespace Vulkan {

#define VULKAN_FN(fn) WEAK PFN_##fn fn;
#include "vulkan_functions.h"
#undef VULKAN_FN

void WEAK load_vulkan_functions(VkInstance instance) {
    #define VULKAN_FN(fn) fn = (PFN_##fn)vkGetInstanceProcAddr(instance, pName);
    #include "vulkan_functions.h"
    #undef VULKAN_FN
}

extern WEAK halide_device_interface_t vulkan_device_interface;

WEAK const char *get_vulkan_error_name(VkResult error);

// An Vulkan context/queue/synchronization lock defined in
// this module with weak linkage
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
WEAK int halide_acquire_vulkan_context(void *user_context, VkInstance *instance,
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

typedef enum VkPhysicalDeviceType {
    VK_PHYSICAL_DEVICE_TYPE_OTHER = 0,
    VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU = 1,
    VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU = 2,
    VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU = 3,
    VK_PHYSICAL_DEVICE_TYPE_CPU = 4,
    VK_PHYSICAL_DEVICE_TYPE_BEGIN_RANGE = VK_PHYSICAL_DEVICE_TYPE_OTHER,
    VK_PHYSICAL_DEVICE_TYPE_END_RANGE = VK_PHYSICAL_DEVICE_TYPE_CPU,
    VK_PHYSICAL_DEVICE_TYPE_RANGE_SIZE = (VK_PHYSICAL_DEVICE_TYPE_CPU - VK_PHYSICAL_DEVICE_TYPE_OTHER + 1),
    VK_PHYSICAL_DEVICE_TYPE_MAX_ENUM = 0x7FFFFFFF
} VkPhysicalDeviceType;

typedef struct VkInstanceCreateInfo {
    VkStructureType             sType;
    const void*                 pNext;
    VkInstanceCreateFlags       flags;
    const VkApplicationInfo*    pApplicationInfo;
    uint32_t                    enabledLayerCount;
    const char* const*          ppEnabledLayerNames;
    uint32_t                    enabledExtensionCount;
    const char* const*          ppEnabledExtensionNames;
} VkInstanceCreateInfo;

	typedef VkResult (VKAPI_PTR *PFN_vkCreateDevice)(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDevice* pDevice);
	// Making a queue	
typedef enum VkQueueFlagBits {
    VK_QUEUE_GRAPHICS_BIT = 0x00000001,
    VK_QUEUE_COMPUTE_BIT = 0x00000002,
    VK_QUEUE_TRANSFER_BIT = 0x00000004,
    VK_QUEUE_SPARSE_BINDING_BIT = 0x00000008,
    VK_QUEUE_FLAG_BITS_MAX_ENUM = 0x7FFFFFFF
} VkQueueFlagBits;

typedef VkFlags VkQueueFlags;

typedef struct VkQueueFamilyProperties {
    VkQueueFlags    queueFlags;
    uint32_t        queueCount;
    uint32_t        timestampValidBits;
    VkExtent3D      minImageTransferGranularity;
} VkQueueFamilyProperties;

typedef void (VKAPI_PTR *PFN_vkGetDeviceQueue)(VkDevice device, uint32_t queueFamilyIndex, uint32_t queueIndex, VkQueue* pQueue);

    }
    *instance = cached_instance;
    *device = cached_device;
    *queue = cached_queue;
    return 0;
}

WEAK int halide_release_vulkan_context(void *user_context) {
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
					     error(0) {
        
        while (__sync_lock_test_and_set(&thread_lock, 1)) { }

        if (VkCreateInstance == NULL) {
            load_libvulkan(user_context);
        }
	
        error = halide_acquire_vulkan_context(user_context, &instance, &context, &queue);
        halide_assert(user_context, context != NULL && cmd_queue != NULL);

	__sync_lock_release(&thread_lock);
    }

    INLINE ~VkContext() {
        halide_release_vukan_context(user_context, insance, device, queue);
    }

    // For now, this is always NULL
    INLINE const VkAllocationCallbacks *allocation_callbacks() { return NULL; }
};

// Structure to hold the state of a module attached to the context.
// Also used as a linked-list to keep track of all the different
// modules that are attached to a context in order to release them all
// when then context is released.
struct module_state {
    cl_program program;
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
        << "CL: halide_vulkan_init_kernels (user_context: " << user_context
        << ", state_ptr: " << state_ptr
        << ", program: " << (void *)src
        << ", size: " << size << "\n";

    ClContext ctx(user_context);
    if (ctx.error != CL_SUCCESS) {
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
        (*state)->program = NULL;
        (*state)->next = state_list;
        state_list = *state;
    }

    // Create the program if necessary. TODO: The program object needs to not
    // only already exist, but be created for the same context/device as the
    // calling context/device.
    if (!(*state && (*state)->program) && size > 1) {
        cl_int err = 0;
        cl_device_id dev;

        err = clGetContextInfo(ctx.context, CL_CONTEXT_DEVICES, sizeof(dev), &dev, NULL);
        if (err != CL_SUCCESS) {
            error(user_context) << "CL: clGetContextInfo(CL_CONTEXT_DEVICES) failed: "
                                << get_vulkan_error_name(err);
            return err;
        }

        cl_device_id devices[] = { dev };

        // Get the max constant buffer size supported by this Vulkan implementation.
        cl_ulong max_constant_buffer_size = 0;
        err = clGetDeviceInfo(dev, CL_DEVICE_MAX_CONSTANT_BUFFER_SIZE, sizeof(max_constant_buffer_size), &max_constant_buffer_size, NULL);
        if (err != CL_SUCCESS) {
            error(user_context) << "CL: clGetDeviceInfo (CL_DEVICE_MAX_CONSTANT_BUFFER_SIZE) failed: "
                                << get_vulkan_error_name(err);
            return err;
        }
        // Get the max number of constant arguments supported by this Vulkan implementation.
        cl_uint max_constant_args = 0;
        err = clGetDeviceInfo(dev, CL_DEVICE_MAX_CONSTANT_ARGS, sizeof(max_constant_args), &max_constant_args, NULL);
        if (err != CL_SUCCESS) {
            error(user_context) << "CL: clGetDeviceInfo (CL_DEVICE_MAX_CONSTANT_ARGS) failed: "
                                << get_vulkan_error_name(err);
            return err;
        }

        // Build the compile argument options.
        stringstream options(user_context);
        options << "-D MAX_CONSTANT_BUFFER_SIZE=" << max_constant_buffer_size
                << " -D MAX_CONSTANT_ARGS=" << max_constant_args;

        const char * sources[] = { src };
        debug(user_context) << "    clCreateProgramWithSource -> ";
        cl_program program = clCreateProgramWithSource(ctx.context, 1, &sources[0], NULL, &err );
        if (err != CL_SUCCESS) {
            debug(user_context) << get_vulkan_error_name(err) << "\n";
            error(user_context) << "CL: clCreateProgramWithSource failed: "
                                << get_vulkan_error_name(err);
            return err;
        } else {
            debug(user_context) << (void *)program << "\n";
        }
        (*state)->program = program;

        debug(user_context) << "    clBuildProgram " << (void *)program
                            << " " << options.str() << "\n";
        err = clBuildProgram(program, 1, devices, options.str(), NULL, NULL );
        if (err != CL_SUCCESS) {

            // Allocate an appropriately sized buffer for the build log.
            char buffer[8192];

            // Get build log
            if (clGetProgramBuildInfo(program, dev,
                                      CL_PROGRAM_BUILD_LOG,
                                      sizeof(buffer), buffer,
                                      NULL) == CL_SUCCESS) {
                error(user_context) << "CL: clBuildProgram failed: "
                                    << get_vulkan_error_name(err)
                                    << "\nBuild Log:\n"
                                    << buffer << "\n";
            } else {
                error(user_context) << "clGetProgramBuildInfo failed";
            }

            return err;
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
    debug(user_context) << "CL: halide_vulkan_device_sync (user_context: " << user_context << ")\n";

    ClContext ctx(user_context);
    halide_assert(user_context, ctx.error == CL_SUCCESS);

    #ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
    #endif

    cl_int err = clFinish(ctx.cmd_queue);
    if (err != CL_SUCCESS) {
        error(user_context) << "CL: clFinish failed: "
                            << get_vulkan_error_name(err);
        return err;
    }

    #ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
    #endif

    return CL_SUCCESS;
}

WEAK int halide_vulkan_device_release(void *user_context) {
    debug(user_context)
        << "CL: halide_vulkan_device_release (user_context: " << user_context << ")\n";

    int err;
    cl_context ctx;
    cl_command_queue q;
    err = halide_acquire_cl_context(user_context, &ctx, &q, false);
    if (cached_instance != NULL) {
      // SYNC

        // Unload the modules attached to this context. Note that the list
        // nodes themselves are not freed, only the program objects are
        // released. Subsequent calls to halide_init_kernels might re-create
        // the program object using the same list node to store the program
        // object.
        module_state *state = state_list;
        while (state) {
            if (state->program) {
                debug(user_context) << "    clReleaseProgram " << state->program << "\n";
                err = clReleaseProgram(state->program);
                halide_assert(user_context, err == CL_SUCCESS);
                state->program = NULL;
            }
            state = state->next;
        }
	
	// release queue
	queue = NULL;
	// release device
	device = NULL;

        vkDestroyInstance(cached_instance, NULL);
	cached_instance = NULL;
    }

    halide_release_vulkan_context(user_context);

    return 0;
}

WEAK int halide_vulkan_device_malloc(void *user_context, halide_buffer_t* buf) {
#if 0
typedef struct VkBufferCreateInfo {
    VkStructureType        sType;
    const void*            pNext;
    VkBufferCreateFlags    flags;
    VkDeviceSize           size;
    VkBufferUsageFlags     usage;
    VkSharingMode          sharingMode;
    uint32_t               queueFamilyIndexCount;
    const uint32_t*        pQueueFamilyIndices;
} VkBufferCreateInfo;
#endif

    debug(user_context)
        << "halide_vulkan_device_malloc (user_context: " << user_context
        << ", buf: " << buf << ")\n";

    VulkanContext context(user_context);
#if 0
    ClContext ctx(user_context);
    if (ctx.error != CL_SUCCESS) {
        return ctx.error;
    }
#endif

    size_t size = buf->size_in_bytes();
    halide_assert(user_context, size != 0);
    if (buf->device) {
        halide_assert(user_context, validate_device_pointer(user_context, buf, size));
        return 0;
    }

    for (int i = 0; i < buf->dimensions; i++) {
        halide_assert(user_context, buf->dim[i].stride >= 0);
    }


    debug(user_context) << "    allocating " << *buf << "\n";

    #ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
    #endif

    VkBufferCreateInfo args_info {
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, NULL,
	0,
	size,
	// TODO: verify next flags
	VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
	VK_SHARING_MODE_EXCLUSIVE,
	0, NULL
    };
    VkBuffer result;
    VkResult error = vkCreateBuffer(context.vulkan_device(), &args_info, NULL, &result);

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
WEAK int do_multidimensional_copy(void *user_context, const ClContext &ctx,
                             const device_copy &c,
                             uint64_t off, int d, bool d_to_h) {
    if (d > MAX_COPY_DIMS) {
        error(user_context) << "Buffer has too many dimensions to copy to/from GPU\n";
        return -1;
    } else if (d == 0) {
        cl_int err = 0;
        const char *copy_name = d_to_h ? "clEnqueueReadBuffer" : "clEnqueueWriteBuffer";
        debug(user_context) << "    " << copy_name << " "
                            << (void *)c.src << " -> " << (void *)c.dst << ", " << c.chunk_size << " bytes\n";
        if (d_to_h) {
            err = clEnqueueReadBuffer(ctx.cmd_queue, (cl_mem)c.src,
                                      CL_FALSE, off, c.chunk_size, (void *)c.dst,
                                      0, NULL, NULL);
        } else {
            err = clEnqueueWriteBuffer(ctx.cmd_queue, (cl_mem)c.dst,
                                       CL_FALSE, off, c.chunk_size, (void *)c.src,
                                       0, NULL, NULL);
        }
        if (err) {
            error(user_context) << "CL: " << copy_name << " failed: " << get_vulkan_error_name(err);
            return (int)err;
        }
    }
#ifdef ENABLE_VULKAN_11
    else if (d == 2) {
        // Vulkan 1.1 supports stride-aware memory transfers up to 3D, so we
        // can deal with the 2 innermost strides with Vulkan.

        cl_int err = 0;

        size_t offset[3] = { (size_t) off, 0, 0 };
        size_t region[3] = { (size_t) c.chunk_size, (size_t) c.extent[0], (size_t) c.extent[1] };

        const char *copy_name = d_to_h ? "clEnqueueReadBufferRect" : "clEnqueueWriteBufferRect";
        debug(user_context) << "    " << copy_name << " "
                            << (void *)c.src << " -> " << (void *)c.dst
                            << ", " << c.chunk_size << " bytes\n";

        if (d_to_h) {
            err = clEnqueueReadBufferRect(ctx.cmd_queue, (cl_mem)c.src, CL_FALSE,
                                          offset, offset, region,
                                          c.stride_bytes[0], c.stride_bytes[1],
                                          c.stride_bytes[0], c.stride_bytes[1],
                                          (void *)c.dst,
                                          0, NULL, NULL);
        } else {

            err = clEnqueueWriteBufferRect(ctx.cmd_queue, (cl_mem)c.dst, CL_FALSE,
                                           offset, offset, region,
                                           c.stride_bytes[0], c.stride_bytes[1],
                                           c.stride_bytes[0], c.stride_bytes[1],
                                           (void *)c.src,
                                           0, NULL, NULL);


        }
        if (err) {
            error(user_context) << "CL: " << copy_name << " failed: " << get_vulkan_error_name(err);
            return (int)err;
        }
    }
#endif
    else {
        for (int i = 0; i < (int)c.extent[d-1]; i++) {
            int err = do_multidimensional_copy(user_context, ctx, c, off, d-1, d_to_h);
            off += c.stride_bytes[d-1];
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
        << "CL: halide_vulkan_copy_to_device (user_context: " << user_context
        << ", buf: " << buf << ")\n";

    // Acquire the context so we can use the command queue. This also avoids multiple
    // redundant calls to clEnqueueWriteBuffer when multiple threads are trying to copy
    // the same buffer.
    ClContext ctx(user_context);
    if (ctx.error != CL_SUCCESS) {
        return ctx.error;
    }

    #ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
    #endif

    halide_assert(user_context, buf->host && buf->device);
    halide_assert(user_context, validate_device_pointer(user_context, buf));

    device_copy c = make_host_to_device_copy(buf);

    do_multidimensional_copy(user_context, ctx, c, 0, buf->dimensions, false);

    // The writes above are all non-blocking, so empty the command
    // queue before we proceed so that other host code won't write
    // to the buffer while the above writes are still running.
    clFinish(ctx.cmd_queue);

    #ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
    #endif

    return 0;
}

WEAK int halide_vulkan_copy_to_host(void *user_context, halide_buffer_t* buf) {
    debug(user_context)
        << "CL: halide_copy_to_host (user_context: " << user_context
        << ", buf: " << buf << ")\n";

    // Acquire the context so we can use the command queue. This also avoids multiple
    // redundant calls to clEnqueueReadBuffer when multiple threads are trying to copy
    // the same buffer.
    ClContext ctx(user_context);
    if (ctx.error != CL_SUCCESS) {
        return ctx.error;
    }

    #ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
    #endif

    halide_assert(user_context, buf->host && buf->device);
    halide_assert(user_context, validate_device_pointer(user_context, buf));

    device_copy c = make_device_to_host_copy(buf);

    do_multidimensional_copy(user_context, ctx, c, 0, buf->dimensions, true);

    // The reads above are all non-blocking, so empty the command
    // queue before we proceed so that other host code won't read
    // bad data.
    clFinish(ctx.cmd_queue);

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
        << "CL: halide_vulkan_run (user_context: " << user_context << ", "
        << "entry: " << entry_name << ", "
        << "blocks: " << blocksX << "x" << blocksY << "x" << blocksZ << ", "
        << "threads: " << threadsX << "x" << threadsY << "x" << threadsZ << ", "
        << "shmem: " << shared_mem_bytes << "\n";


    cl_int err;
    ClContext ctx(user_context);
    if (ctx.error != CL_SUCCESS) {
        return ctx.error;
    }

    #ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
    #endif

    // Create kernel object for entry_name from the program for this module.
    halide_assert(user_context, state_ptr);
    cl_program program = ((module_state*)state_ptr)->program;

    halide_assert(user_context, program);
    debug(user_context) << "    clCreateKernel " << entry_name << " -> ";
    cl_kernel f = clCreateKernel(program, entry_name, &err);
    if (err != CL_SUCCESS) {
        debug(user_context) << get_vulkan_error_name(err) << "\n";
        error(user_context) << "CL: clCreateKernel " << entry_name << " failed: "
                            << get_vulkan_error_name(err) << "\n";
        return err;
    } else {
        #ifdef DEBUG_RUNTIME
        uint64_t t_create_kernel = halide_current_time_ns(user_context);
        debug(user_context) << "    Time: " << (t_create_kernel - t_before) / 1.0e6 << " ms\n";
        #endif
    }

    // Pack dims
    size_t global_dim[3] = {(size_t) blocksX*threadsX,  (size_t) blocksY*threadsY, (size_t) blocksZ*threadsZ};
    size_t local_dim[3] = {(size_t) threadsX, (size_t) threadsY, (size_t) threadsZ};

    // Set args
    int i = 0;
    while (arg_sizes[i] != 0) {
        debug(user_context) << "    clSetKernelArg " << i
                            << " " << (int)arg_sizes[i]
                            << " [" << (*((void **)args[i])) << " ...] "
                            << arg_is_buffer[i] << "\n";
        void *this_arg = args[i];
        cl_int err;

        if (arg_is_buffer[i]) {
            halide_assert(user_context, arg_sizes[i] == sizeof(uint64_t));
            uint64_t vulkan_handle = ((halide_buffer_t *)this_arg)->device;
            debug(user_context) << "Mapped dev handle is: " << (void *)vulkan_handle << "\n";
            // In 32-bit mode, vulkan only wants the bottom 32 bits of
            // the handle, so use sizeof(void *) instead of
            // arg_sizes[i] below.
            err = clSetKernelArg(f, i, sizeof(void *), &vulkan_handle);
        } else {
            err = clSetKernelArg(f, i, arg_sizes[i], this_arg);
        }


        if (err != CL_SUCCESS) {
            error(user_context) << "CL: clSetKernelArg failed: "
                                << get_vulkan_error_name(err);
            return err;
        }
        i++;
    }
    // Set the shared mem buffer last
    // Always set at least 1 byte of shmem, to keep the launch happy
    debug(user_context)
        << "    clSetKernelArg " << i << " " << shared_mem_bytes << " [NULL]\n";
    err = clSetKernelArg(f, i, (shared_mem_bytes > 0) ? shared_mem_bytes : 1, NULL);
    if (err != CL_SUCCESS) {
        error(user_context) << "CL: clSetKernelArg failed "
                            << get_vulkan_error_name(err);
        return err;
    }

    // Launch kernel
    debug(user_context)
        << "    clEnqueueNDRangeKernel "
        << blocksX << "x" << blocksY << "x" << blocksZ << ", "
        << threadsX << "x" << threadsY << "x" << threadsZ << " -> ";
    err = clEnqueueNDRangeKernel(ctx.cmd_queue, f,
                                 // NDRange
                                 3, NULL, global_dim, local_dim,
                                 // Events
                                 0, NULL, NULL);
    debug(user_context) << get_vulkan_error_name(err) << "\n";
    if (err != CL_SUCCESS) {
        error(user_context) << "CL: clEnqueueNDRangeKernel failed: "
                            << get_vulkan_error_name(err) << "\n";
        return err;
    }

    debug(user_context) << "    Releasing kernel " << (void *)f << "\n";
    clReleaseKernel(f);
    debug(user_context) << "    clReleaseKernel finished" << (void *)f << "\n";

    #ifdef DEBUG_RUNTIME
    err = clFinish(ctx.cmd_queue);
    if (err != CL_SUCCESS) {
        error(user_context) << "CL: clFinish failed (" << err << ")\n";
        return err;
    }
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
#if 0 && DEBUG_RUNTIME
    if (!validate_device_pointer(user_context, buf)) {
        buf->device = 0;
        buf->device_interface->impl->release_module();
        buf->device_interface = NULL;
        return -3;
    }
#endif
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
    halide_vulkan_wrap_cl_mem,
    halide_vulkan_detach_cl_mem,
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
    halide_device_wrap_native,
    halide_device_detach_native,
    &vulkan_device_interface_impl
};

}}}} // namespace Halide::Runtime::Internal::Vulkan
