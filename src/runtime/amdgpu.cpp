#include "HalideRuntimeAMDGPU.h"
#include "device_buffer_utils.h"
#include "device_interface.h"
#include "printer.h"
#include "mini_amdgpu.h"
#include "scoped_spin_lock.h"

#define INLINE inline __attribute__((always_inline))

namespace Halide { namespace Runtime { namespace Internal {namespace Amdgpu {

#define HIP_FN(ret, fn, args) WEAK ret (HIPAPI *fn)args;

#include "hip_functions.h"

WEAK void *lib_hip = NULL;

extern "C" WEAK void *halide_amdgpu_get_symbol(void *user_context, const char *name) {
    // Only try to load the library if we can't already get the symbol
    // from the library. Even if the library is NULL, the symbols may
    // already be available in the process.
    void *symbol = halide_get_library_symbol(lib_hip, name);
    if (symbol) {
        return symbol;
    }

    const char *lib_names[] = {
        "libhip_hcc.so",
    };
    for (size_t i = 0; i < sizeof(lib_names) / sizeof(lib_names[0]); i++) {
        lib_hip = halide_load_library(lib_names[i]);
        if (lib_hip) {
            debug(user_context) << "    Loaded HIP runtime library: " << lib_names[i] << "\n";
            break;
        }
    }

    return halide_get_library_symbol(lib_hip, name);
}

template <typename T>
INLINE T get_amdgpu_symbol(void *user_context, const char *name, bool optional = false) {
    T s = (T)halide_amdgpu_get_symbol(user_context, name);
    if (!optional && !s) {
        error(user_context) << "HIP API not found: " << name << "\n";
    }
    return s;
}

// Load a HIP shared object and get the HIP API function pointers from it.
WEAK void load_libhip(void *user_context) {
    debug(user_context) << "    load_libhip (user_context: " << user_context << ")\n";
    halide_assert(user_context, hipInit == NULL);

    #define HIP_FN(ret, fn, args) fn = get_amdgpu_symbol<ret (HIPAPI *)args>(user_context, #fn);

    #include "hip_functions.h"
}

extern WEAK halide_device_interface_t amdgpu_device_interface;

WEAK const char *get_error_name(hipError_t error);
WEAK hipError_t create_amdgpu_context(void *user_context, hipCtx_t *ctx);

// A amdgpu context defined in this module with weak linkage
hipCtx_t WEAK context = 0;
// This spinlock protexts the above context variable.
volatile int WEAK context_lock = 0;

}}}} // namespace Halide::Runtime::Internal::Amdgpu

using namespace Halide::Runtime::Internal;
using namespace Halide::Runtime::Internal::Amdgpu;

extern "C" {
WEAK int halide_amdgpu_acquire_context(void *user_context, hipCtx_t *ctx, bool create = true) {
    // TODO: Should we use a more "assertive" assert? these asserts do
    // not block execution on failure.
    halide_assert(user_context, ctx != NULL);

    // If the context has not been initialized, initialize it now.
    halide_assert(user_context, &context != NULL);

    hipCtx_t local_val;
    __atomic_load(&context, &local_val, __ATOMIC_ACQUIRE);
    if (local_val == NULL) {
        if (!create) {
            *ctx = NULL;
            return 0;
        }

        {
            ScopedSpinLock spinlock(&context_lock);

            __atomic_load(&context, &local_val, __ATOMIC_ACQUIRE);
            if (local_val == NULL) {
                hipError_t error = create_amdgpu_context(user_context, &local_val);
                if (error != hipSuccess) {
                    __sync_lock_release(&context_lock);
                    return error;
                }
            }
            __atomic_store(&context, &local_val, __ATOMIC_RELEASE);
        }  // spinlock
    }

    *ctx = local_val;
    return 0;
}

WEAK int halide_amdgpu_release_context(void *user_context) {
    return 0;
}

WEAK int halide_amdgpu_get_stream(void *user_context, hipCtx_t ctx, hipStream_t *stream) {
    *stream = 0;
    return 0;
}

} // extern "C"

namespace Halide { namespace Runtime { namespace Internal { namespace Amdgpu {

class Context {
    void *user_context;

public:
    hipCtx_t context;
    int error;

    INLINE Context(void *user_context) : user_context(user_context),
                                        context(NULL),
                                        error(hipSuccess) {
        if(hipInit == NULL) {
            load_libhip(user_context);
        }

#ifdef DEBUG_RUNTIME
        halide_start_clock(user_context);
#endif

        error = halide_amdgpu_acquire_context(user_context, &context);
        halide_assert(user_context, context != NULL);
        if(error != 0) {
            return;
        }

        error = hipCtxPushCurrent(context);
    }

    INLINE ~Context() {
        hipCtx_t old;
        hipCtxPopCurrent(&old);

        halide_amdgpu_release_context(user_context);
    }
};

struct module_state {
    hipCtx_t context;
    hipModule_t module;
    module_state *next;
};

struct registered_filters {
    module_state *modules;
    registered_filters *next;
};

WEAK registered_filters *filters_list = NULL;

volatile int WEAK filters_list_lock = 0;

WEAK module_state *find_module_for_context(const registered_filters *filters, hipCtx_t ctx) {
    module_state *modules = filters->modules;
    while(modules != NULL) {
        if(modules->context == ctx) {
            return modules;
        }
        modules = modules->next;
    }
    return NULL;
}

WEAK hipError_t create_amdgpu_context(void *user_context, hipCtx_t *ctx) {
    hipError_t err = hipInit(0);
    if(err != hipSuccess) {
        error(user_context) << "AMDGPU: hipInit failed: "
                            << get_error_name(err);
        return err;
    }

    int deviceCount = 0;
    err = hipGetDeviceCount(&deviceCount);
    if(err != hipSuccess) {
        error(user_context) << "AMDGPU: hipGetDeviceCount failed: "
                            << get_error_name(err);
        return err;
    }
    if(deviceCount <= 0) {
        halide_error(user_context, "AMDGPU: No devices avaiable");
        return hipErrorNoDevice;
    }

    int device = halide_get_gpu_device(user_context);

    if (device == -1 && deviceCount == 1) {
        device = 0;
    } else if (device == -1) {
        debug(user_context) << "AMDGPU: Multiple AMDGPU devices detected. Selecting the one with the most cores.\n";
        int best_core_count = 0;
        for (int i = 0; i < deviceCount; i++) {
            hipDevice_t dev;
            hipError_t status = hipDeviceGet(&dev, i);
            if (status != hipSuccess) {
                debug(user_context) << "      Failed to get device " << i << "\n";
                continue;
            }
            int core_count = 0;
            status = hipDeviceGetAttribute(&core_count, hipDeviceAttributeMaxThreadsPerMultiProcessor, dev);
            debug(user_context) << "      Device " << i << " has " << core_count << " cores\n";
            if (status != hipSuccess) {
                continue;
            }
            if (core_count >= best_core_count) {
                device = i;
                best_core_count = core_count;
            }
        }
    }

    // Get device
    hipDevice_t dev;
    hipError_t status = hipDeviceGet(&dev, device);
    if (status != hipSuccess) {
        halide_error(user_context, "AMDGPU: Failed to get device\n");
        return status;
    }

    debug(user_context) <<  "    Got device " << dev << "\n";

    // Dump device attributes
    #ifdef DEBUG_RUNTIME
    {
        char name[256];
        name[0] = 0;
        err = hipDeviceGetName(name, 256, dev);
        debug(user_context) << "      " << name << "\n";

        if (err != hipSuccess) {
            error(user_context) << "AMDGPU: hipDeviceGetName failed: "
                                << get_error_name(err);
            return err;
        }

        size_t memory = 0;
        err = hipDeviceTotalMem(&memory, dev);
        debug(user_context) << "      total memory: " << (int)(memory >> 20) << " MB\n";

        if (err != hipSuccess) {
            error(user_context) << "AMDGPU: hipDeviceTotalMem failed: "
                                << get_error_name(err);
            return err;
        }

        // Declare variables for other state we want to query.
        int max_threads_per_block = 0, warp_size = 0, num_cores = 0;
        int max_block_size[] = {0, 0, 0};
        int max_grid_size[] = {0, 0, 0};
        int max_shared_mem = 0, max_constant_mem = 0;
        int cc_major = 0, cc_minor = 0;

        struct {int *dst; hipDeviceAttribute_t attr;} attrs[] = {
            {&max_threads_per_block, hipDeviceAttributeMaxThreadsPerBlock},
            {&warp_size,             hipDeviceAttributeWarpSize},
            {&num_cores,             hipDeviceAttributeMultiprocessorCount},
            {&max_block_size[0],     hipDeviceAttributeMaxBlockDimX},
            {&max_block_size[1],     hipDeviceAttributeMaxBlockDimY},
            {&max_block_size[2],     hipDeviceAttributeMaxBlockDimZ},
            {&max_grid_size[0],      hipDeviceAttributeMaxGridDimX},
            {&max_grid_size[1],      hipDeviceAttributeMaxGridDimY},
            {&max_grid_size[2],      hipDeviceAttributeMaxGridDimZ},
            {&max_shared_mem,        hipDeviceAttributeMaxSharedMemoryPerBlock},
            {&max_constant_mem,      hipDeviceAttributeTotalConstantMemory},
            {&cc_major,              hipDeviceAttributeComputeCapabilityMajor},
            {&cc_minor,              hipDeviceAttributeComputeCapabilityMinor},
            {NULL,                   hipDeviceAttributeMax}};

        // Do all the queries.
        for (int i = 0; attrs[i].dst; i++) {
            err = hipDeviceGetAttribute(attrs[i].dst, attrs[i].attr, dev);
            if (err != hipSuccess) {
                error(user_context)
                    << "AMDGPU: hipDeviceGetAttribute failed ("
                    << get_error_name(err)
                    << ") for attribute " << (int)attrs[i].attr;
                return err;
            }
        }

        // threads per core is a function of the compute capability
        int threads_per_core = (cc_major == 1 ? 8 :
                                cc_major == 2 ? (cc_minor == 0 ? 32 : 48) :
                                cc_major == 3 ? 192 :
                                cc_major == 5 ? 128 : 0);

        debug(user_context)
            << "      max threads per block: " << max_threads_per_block << "\n"
            << "      warp size: " << warp_size << "\n"
            << "      max block size: " << max_block_size[0]
            << " " << max_block_size[1] << " " << max_block_size[2] << "\n"
            << "      max grid size: " << max_grid_size[0]
            << " " << max_grid_size[1] << " " << max_grid_size[2] << "\n"
            << "      max shared memory per block: " << max_shared_mem << "\n"
            << "      max constant memory per block: " << max_constant_mem << "\n"
            << "      compute capability " << cc_major << "." << cc_minor << "\n"
            << "      workitems: " << num_cores << " x " << threads_per_core << " = " << threads_per_core << "\n";
    }
    #endif

    // Create context
    debug(user_context) <<  "    hipCtxCreate " << dev << " -> ";
    err = hipCtxCreate(ctx, 0, dev);
    if (err != hipSuccess) {
        debug(user_context) << get_error_name(err) << "\n";
        error(user_context) << "AMDGPU: hipCtxCreate failed: "
                            << get_error_name(err);
        return err;
    } else {
        int version = 0;
        hipCtxGetApiVersion(*ctx, &version);
        debug(user_context) << *ctx << "(" << version << ")\n";
    }
    // Creation automatically pushes the context, but we'll pop to allow the caller
    // to decide when to push.
    hipCtx_t dummy;
    err = hipCtxPopCurrent(&dummy);
    if (err != hipSuccess) {
      error(user_context) << "AMDGPU: hipCtxPopCurrent failed: "
                          << get_error_name(err);
      return err;
    }

    return hipSuccess;
}

WEAK bool validate_device_pointer(void *user_context, halide_buffer_t* buf, size_t size=0) {
#if defined(BITS_32) || !ENABLE_POINTER_VALIDATION
    return true;
#else
    if (buf->device == 0)
        return true;

    hipDeviceptr_t dev_ptr = (hipDeviceptr_t)buf->device;

#define hipPointerAttributeContext 1

    hipCtx_t ctx;
    hipError_t result = hipPointerGetAttributes(&ctx, hipPointerAttributeContext, dev_ptr);
    if (result != hipSuccess) {
        error(user_context) << "Bad device pointer " << (void *)dev_ptr
                            << ": hipPointerGetAttributes returned "
                            << get_error_name(result);
        return false;
    }
    return true;
#endif
}

}}}} // namespace Halide::Runtime::Internal


extern "C" {
WEAK int halide_amdgpu_initialize_kernels(void *user_context, void **state_ptr, const char* ptx_src, int size) {
    debug(user_context) << "AMDGPU: halide_amdgpu_initialize_kernels (user_context: " << user_context
                        << ", state_ptr: " << state_ptr
                        << ", ptx_src: " << (void *)ptx_src
                        << ", size: " << size << "\n";

    Context ctx(user_context);
    if (ctx.error != 0) {
        return ctx.error;
    }

    #ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
    #endif


    halide_assert(user_context, &filters_list_lock != NULL);
    {
        ScopedSpinLock spinlock(&filters_list_lock);

        // Create the state object if necessary. This only happens once, regardless
        // of how many times halide_initialize_kernels/halide_release is called.
        // halide_release traverses this list and releases the module objects, but
        // it does not modify the list nodes created/inserted here.
        registered_filters **filters = (registered_filters**)state_ptr;
        if (!(*filters)) {
            *filters = (registered_filters*)malloc(sizeof(registered_filters));
            (*filters)->modules = NULL;
            (*filters)->next = filters_list;
            filters_list = *filters;
        }

        // Create the module itself if necessary.
        module_state *loaded_module = find_module_for_context(*filters, ctx.context);
        if (loaded_module == NULL) {
            loaded_module = (module_state *)malloc(sizeof(module_state));
            debug(user_context) <<  "    hipModuleLoadData " << (void *)ptx_src << ", " << size << " -> ";

            hipJitOption options[] = { hipJitOptionMaxRegisters };
            unsigned int max_regs_per_thread = 64;

            // A hack to enable control over max register count for
            // testing. This should be surfaced in the schedule somehow
            // instead.
            char *regs = getenv("HL_AMDGPU_JIT_MAX_REGISTERS");
            if (regs) {
                max_regs_per_thread = atoi(regs);
            }
            void *optionValues[] = { (void*)(uintptr_t) max_regs_per_thread };
            hipError_t err = hipModuleLoadDataEx(&loaded_module->module, ptx_src, 1, options, optionValues);

            if (err != hipSuccess) {
                free(loaded_module);
                error(user_context) << "AMDGPU: hipModuleLoadData failed: "
                                    << get_error_name(err);
                return err;
            } else {
                debug(user_context) << (void *)(loaded_module->module) << "\n";
            }
            loaded_module->context = ctx.context;
            loaded_module->next = (*filters)->modules;
            (*filters)->modules = loaded_module;
        }
    } // spinlock

    #ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
    #endif

    return 0;
}

WEAK int halide_amdgpu_device_free(void *user_context, halide_buffer_t* buf) {
    // halide_device_free, at present, can be exposed to clients and they
    // should be allowed to call halide_device_free on any halide_buffer_t
    // including ones that have never been used with a GPU.
    if (buf->device == 0) {
        return 0;
    }

    hipDeviceptr_t dev_ptr = (hipDeviceptr_t)buf->device;

    debug(user_context)
        <<  "AMDGPU: halide_amdgpu_device_free (user_context: " << user_context
        << ", buf: " << buf << ")\n";

    Context ctx(user_context);
    if (ctx.error != hipSuccess)
        return ctx.error;

    #ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
    #endif

    halide_assert(user_context, validate_device_pointer(user_context, buf));

    debug(user_context) <<  "    cuMemFree " << (void *)(dev_ptr) << "\n";
    hipError_t err = hipFree(dev_ptr);
    // If cuMemFree fails, it isn't likely to succeed later, so just drop
    // the reference.
    buf->device_interface->impl->release_module();
    buf->device_interface = NULL;
    buf->device = 0;
    if (err != hipSuccess) {
        // We may be called as a destructor, so don't raise an error here.
        return err;
    }

    #ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
    #endif

    return 0;
}

WEAK int halide_amdgpu_device_release(void *user_context) {
    debug(user_context)
        << "AMDGPU: halide_amdgpu_device_release (user_context: " <<  user_context << ")\n";

    int err;
    hipCtx_t ctx;
    err = halide_amdgpu_acquire_context(user_context, &ctx, false);
    if (err != hipSuccess) {
        return err;
    }

    if (ctx) {
        // It's possible that this is being called from the destructor of
        // a static variable, in which case the driver may already be
        // shutting down.
        err = hipCtxPushCurrent(ctx);
        if (err != hipSuccess) {
            err = hipCtxSynchronize();
        }
        halide_assert(user_context, err == hipSuccess || err == hipErrorDeinitialized);

        {
            ScopedSpinLock spinlock(&filters_list_lock);

            // Unload the modules attached to this context. Note that the list
            // nodes themselves are not freed, only the module objects are
            // released. Subsequent calls to halide_init_kernels might re-create
            // the program object using the same list node to store the module
            // object.
            registered_filters *filters = filters_list;
            while (filters) {
                module_state **prev_ptr = &filters->modules;
                module_state *loaded_module = filters->modules;
                while (loaded_module != NULL) {
                    if (loaded_module->context == ctx) {
                        debug(user_context) << "    hipModuleUnload " << loaded_module->module << "\n";
                        err = hipModuleUnload(loaded_module->module);
                        halide_assert(user_context, err == hipSuccess || err == hipErrorDeinitialized);
                        *prev_ptr = loaded_module->next;
                        free(loaded_module);
                        loaded_module = *prev_ptr;
                    } else {
                        loaded_module = loaded_module->next;
                        prev_ptr = &loaded_module->next;
                    }
                }
                filters = filters->next;
            }
        }  // spinlock

        hipCtx_t old_ctx;
        hipCtxPopCurrent(&old_ctx);

        // Only destroy the context if we own it

        {
            ScopedSpinLock spinlock(&context_lock);

            if (ctx == context) {
                debug(user_context) << "    hipCtxDestroy " << context << "\n";
                err = hipProfilerStop();
                err = hipCtxDestroy(context);
                halide_assert(user_context, err == hipSuccess || err == hipErrorDeinitialized);
                context = NULL;
            }
        }  // spinlock
    }

    halide_amdgpu_release_context(user_context);

    return 0;
}

WEAK int halide_amdgpu_device_malloc(void *user_context, halide_buffer_t *buf) {
    debug(user_context)
        << "AMDGPU: halide_amdgpu_device_malloc (user_context: " << user_context
        << ", buf: " << buf << ")\n";

    Context ctx(user_context);
    if (ctx.error != hipSuccess) {
        return ctx.error;
    }

    size_t size = buf->size_in_bytes();
    halide_assert(user_context, size != 0);
    if (buf->device) {
        // This buffer already has a device allocation
        halide_assert(user_context, validate_device_pointer(user_context, buf, size));
        return 0;
    }

    // Check all strides positive.
    for (int i = 0; i < buf->dimensions; i++) {
        halide_assert(user_context, buf->dim[i].stride >= 0);
    }

    debug(user_context) << "    allocating " << *buf << "\n";

    #ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
    #endif

    hipDeviceptr_t p;
    debug(user_context) << "    hipMalloc " << (uint64_t)size << " -> ";
    hipError_t err = hipMalloc((void**)&p, size);
    if (err != hipSuccess) {
        debug(user_context) << get_error_name(err) << "\n";
        error(user_context) << "AMDGPU: hipMalloc failed: "
                            << get_error_name(err);
        return err;
    } else {
        debug(user_context) << (void *)p << "\n";
    }
    halide_assert(user_context, p);
    buf->device = reinterpret_cast<uint64_t>(p);
    buf->device_interface = &amdgpu_device_interface;
    buf->device_interface->impl->use_module();

    #ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
    #endif

    return 0;
}

namespace {
WEAK int do_multidimensional_copy(void *user_context, const device_copy &c,
                                  uint64_t src, uint64_t dst, int d, bool from_host, bool to_host) {
    if (d > MAX_COPY_DIMS) {
        error(user_context) << "Buffer has too many dimensions to copy to/from GPU\n";
        return -1;
    } else if (d == 0) {
        hipError_t err = hipSuccess;
        const char *copy_name;
        debug(user_context) << "    from " << (from_host ? "host" : "device")
                            << " to " << (to_host ? "host" : "device") << ", "
                            << (void *)src << " -> " << (void *)dst << ", " << c.chunk_size << " bytes\n";
        if (!from_host && to_host) {
            err = hipMemcpyDtoH((void *)dst, (hipDeviceptr_t)src, c.chunk_size);
        } else if (from_host && !to_host) {
            err = hipMemcpyHtoD((hipDeviceptr_t)dst, (void *)src, c.chunk_size);
        } else if (!from_host && !to_host) {
            err = hipMemcpyDtoD((hipDeviceptr_t)dst, (hipDeviceptr_t)src, c.chunk_size);
        } else if (dst != src) {
            // Could reach here if a user called directly into the
            // amdgpu API for a device->host copy on a source buffer
            // with device_dirty = false.
            memcpy((void *)dst, (void *)src, c.chunk_size);
        }
        if (err != hipSuccess) {
            error(user_context) << "AMDGPU: " << copy_name << " failed: " << get_error_name(err);
            return (int)err;
        }
    } else {
        ssize_t src_off = 0, dst_off = 0;
        for (int i = 0; i < (int)c.extent[d-1]; i++) {
            int err = do_multidimensional_copy(user_context, c, src + src_off, dst + dst_off, d - 1, from_host, to_host);
            dst_off += c.dst_stride_bytes[d-1];
            src_off += c.src_stride_bytes[d-1];
            if (err) {
                return err;
            }
        }
    }
    return 0;
}
}

WEAK int halide_amdgpu_buffer_copy(void *user_context, struct halide_buffer_t *src,
                                 const struct halide_device_interface_t *dst_device_interface,
                                 struct halide_buffer_t *dst) {
    // We only handle copies to amdgpu or to host
    halide_assert(user_context, dst_device_interface == NULL ||
                  dst_device_interface == &amdgpu_device_interface);

    if (src->device_dirty() &&
        src->device_interface != &amdgpu_device_interface) {
        halide_assert(user_context, dst_device_interface == &amdgpu_device_interface);
        // If the source is not amdgpu or host memory, ask the source
        // device interface to copy to dst host memory first.
        int err = src->device_interface->impl->buffer_copy(user_context, src, NULL, dst);
        if (err) return err;
        // Now just copy from src to host
        src = dst;
    }

    bool from_host = !src->device_dirty() && src->host;
    bool to_host = !dst_device_interface;

    halide_assert(user_context, from_host || src->device);
    halide_assert(user_context, to_host || dst->device);

    device_copy c = make_buffer_copy(src, from_host, dst, to_host);

    int err = 0;
    {
        Context ctx(user_context);
        if (ctx.error != hipSuccess) {
            return ctx.error;
        }

        debug(user_context)
            << "AMDGPU: halide_amdgpu_buffer_copy (user_context: " << user_context
            << ", src: " << src << ", dst: " << dst << ")\n";

        #ifdef DEBUG_RUNTIME
        uint64_t t_before = halide_current_time_ns(user_context);
        if (!from_host) {
            halide_assert(user_context, validate_device_pointer(user_context, src));
        }
        if (!to_host) {
            halide_assert(user_context, validate_device_pointer(user_context, dst));
        }
        #endif

        err = do_multidimensional_copy(user_context, c, c.src + c.src_begin, c.dst, dst->dimensions, from_host, to_host);

        #ifdef DEBUG_RUNTIME
        uint64_t t_after = halide_current_time_ns(user_context);
        debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
        #endif
    }

    return err;
}

WEAK int halide_amdgpu_device_crop(void *user_context, const struct halide_buffer_t *src,
                                 struct halide_buffer_t *dst) {
    debug(user_context)
        << "AMDGPU: halide_amdgpu_device_crop (user_context: " << user_context
        << ", src: " << src << ", dst: " << dst << ")\n";

    // Pointer arithmetic works fine.
    int64_t offset = 0;
    for (int i = 0; i < src->dimensions; i++) {
        offset += (dst->dim[i].min - src->dim[i].min) * src->dim[i].stride;
    }
    offset *= src->type.bytes();
    dst->device = src->device + offset;
    dst->device_interface = src->device_interface;
    dst->set_device_dirty(src->device_dirty());
    return 0;
}

WEAK int halide_amdgpu_device_release_crop(void *user_context, struct halide_buffer_t *dst) {
    debug(user_context)
        << "AMDGPU: halide_amdgpu_release_crop (user_context: " << user_context
        << ", dst: " << dst << ")\n";
    return 0;
}

WEAK int halide_amdgpu_copy_to_device(void *user_context, halide_buffer_t* buf) {
    return halide_amdgpu_buffer_copy(user_context, buf, &amdgpu_device_interface, buf);
}

WEAK int halide_amdgpu_copy_to_host(void *user_context, halide_buffer_t* buf) {
    return halide_amdgpu_buffer_copy(user_context, buf, NULL, buf);
}

WEAK int halide_amdgpu_device_sync(void *user_context, struct halide_buffer_t *) {
    debug(user_context)
        << "AMDGPU: halide_amdgpu_device_sync (user_context: " << user_context << ")\n";

    Context ctx(user_context);
    if (ctx.error != hipSuccess) {
        return ctx.error;
    }

    #ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
    #endif

    hipError_t err;
    if (hipStreamSynchronize != NULL) {
        hipStream_t stream;
        int result = halide_amdgpu_get_stream(user_context, ctx.context, &stream);
        if (result != 0) {
            error(user_context) << "AMDGPU: In halide_amdgpu_device_sync, halide_amdgpu_get_stream returned " << result << "\n";
        }
        err = hipStreamSynchronize(stream);
    } else {
       err = hipCtxSynchronize();
    }
    if (err != hipSuccess) {
        error(user_context) << "AMDGPU: hipCtxSynchronize failed: "
                            << get_error_name(err);
        return err;
    }

    #ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
    #endif

    return 0;
}
WEAK int halide_amdgpu_run(void *user_context,
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

    debug(user_context) << "AMDGPU: halide_amdgpu_run ("
                        << "user_context: " << user_context << ", "
                        << "entry: " << entry_name << ", "
                        << "blocks: " << blocksX << "x" << blocksY << "x" << blocksZ << ", "
                        << "threads: " << threadsX << "x" << threadsY << "x" << threadsZ << ", "
                        << "shmem: " << shared_mem_bytes << "\n";

    hipError_t err;
    Context ctx(user_context);
    if (ctx.error != hipSuccess) {
        return ctx.error;
    }

    debug(user_context) << "Got context.\n";

    #ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
    #endif

    halide_assert(user_context, state_ptr);
    module_state *loaded_module = find_module_for_context((registered_filters *)state_ptr, ctx.context);
    halide_assert(user_context, loaded_module != NULL);
    hipModule_t mod = loaded_module->module;
    debug(user_context) << "Got module " << mod << "\n";
    halide_assert(user_context, mod);
    hipFunction_t f;
    err = hipModuleGetFunction(&f, mod, entry_name);
    debug(user_context) << "Got function " << f << "\n";
    if (err != hipSuccess) {
        error(user_context) << "AMDGPU: hipModuleGetFunction failed: "
                            << get_error_name(err);
        return err;
    }

    size_t num_args = 0;
    while (arg_sizes[num_args] != 0) {
        debug(user_context) << "    halide_amdgpu_run " << (int)num_args
                            << " " << (int)arg_sizes[num_args]
                            << " [" << (*((void **)args[num_args])) << " ...] "
                            << arg_is_buffer[num_args] << "\n";
        num_args++;
    }

    // We need storage for both the arg and the pointer to it if if
    // has to be translated.
    void** translated_args = (void **)malloc((num_args + 1) * sizeof(void *));
    uint64_t *dev_handles = (uint64_t *)malloc(num_args * sizeof(uint64_t));
    for (size_t i = 0; i <= num_args; i++) { // Get NULL at end.
        if (arg_is_buffer[i]) {
            halide_assert(user_context, arg_sizes[i] == sizeof(uint64_t));
            dev_handles[i] = ((halide_buffer_t *)args[i])->device;
        } else {
            translated_args[i] = args[i];
        }
    }

    hipStream_t stream = NULL;
    // We use whether this routine was defined in the amdgpu driver library
    // as a test for streams support in the amdgpu implementation.
    if (hipStreamSynchronize != NULL) {
        int result = halide_amdgpu_get_stream(user_context, ctx.context, &stream);
        if (result != 0) {
            error(user_context) << "AMDGPU: In halide_amdgpu_run, halide_amdgpu_get_stream returned " << result << "\n";
        }
    }

// TODO adityaatluri, pack args to last argument

    err = hipModuleLaunchKernel(f,
                         blocksX,  blocksY,  blocksZ,
                         threadsX, threadsY, threadsZ,
                         shared_mem_bytes,
                         stream,
                         translated_args,
                         NULL);
    free(dev_handles);
    free(translated_args);
    if (err != hipSuccess) {
        error(user_context) << "AMDGPU: hipModuleLaunchKernel failed: "
                            << get_error_name(err);
        return err;
    }

    #ifdef DEBUG_RUNTIME
    err = hipCtxSynchronize();
    if (err != hipSuccess) {
        error(user_context) << "AMDGPU: hipCtxSynchronize failed: "
                            << get_error_name(err);
        return err;
    }
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
    #endif
    return 0;
}

WEAK int halide_amdgpu_device_and_host_malloc(void *user_context, struct halide_buffer_t *buf) {
    return halide_default_device_and_host_malloc(user_context, buf, &amdgpu_device_interface);
}

WEAK int halide_amdgpu_device_and_host_free(void *user_context, struct halide_buffer_t *buf) {
    return halide_default_device_and_host_free(user_context, buf, &amdgpu_device_interface);
}

WEAK int halide_amdgpu_wrap_device_ptr(void *user_context, struct halide_buffer_t *buf, uint64_t device_ptr) {
    halide_assert(user_context, buf->device == 0);
    if (buf->device != 0) {
        return -2;
    }
    buf->device = device_ptr;
    buf->device_interface = &amdgpu_device_interface;
    buf->device_interface->impl->use_module();
#if DEBUG_RUNTIME
    if (!validate_device_pointer(user_context, buf)) {
        buf->device_interface->impl->release_module();
        buf->device = 0;
        buf->device_interface = NULL;
        return -3;
    }
#endif
    return 0;
}

WEAK int halide_amdgpu_detach_device_ptr(void *user_context, struct halide_buffer_t *buf) {
    if (buf->device == NULL) {
        return 0;
    }
    halide_assert(user_context, buf->device_interface == &amdgpu_device_interface);
    buf->device_interface->impl->release_module();
    buf->device = 0;
    buf->device_interface = NULL;
    return 0;
}

WEAK uintptr_t halide_amdgpu_get_device_ptr(void *user_context, struct halide_buffer_t *buf) {
    if (buf->device == NULL) {
        return 0;
    }
    halide_assert(user_context, buf->device_interface == &amdgpu_device_interface);
    return (uintptr_t)buf->device;
}

WEAK const halide_device_interface_t *halide_amdgpu_device_interface() {
    return &amdgpu_device_interface;
}

namespace {
__attribute__((destructor))
WEAK void halide_amdgpu_cleanup() {
    halide_amdgpu_device_release(NULL);
}
}

} // extern "C" linkage

namespace Halide { namespace Runtime { namespace Internal { namespace Amdgpu {

WEAK const char *get_error_name(hipError_t err) {
    switch(err) {
        case hipSuccess                         : return "hipSuccess";
        case hipErrorOutOfMemory                : return "hipErrorOutOfMemory";
        case hipErrorNotInitialized             : return "hipErrorNotInitialized";
        case hipErrorDeinitialized              : return "hipErrorDeinitialized";
        case hipErrorProfilerDisabled           : return "hipErrorProfilerDisabled";
        case hipErrorProfilerNotInitialized     : return "hipErrorProfilerNotInitialized";
        case hipErrorProfilerAlreadyStarted     : return "hipErrorProfilerAlreadyStarted";
        case hipErrorProfilerAlreadyStopped     : return "hipErrorProfilerAlreadyStopped";
        case hipErrorInvalidImage               : return "hipErrorInvalidImage";
        case hipErrorInvalidContext             : return "hipErrorInvalidContext";
        case hipErrorContextAlreadyCurrent      : return "hipErrorContextAlreadyCurrent";
        case hipErrorMapFailed                  : return "hipErrorMapFailed";
        case hipErrorUnmapFailed                : return "hipErrorUnmapFailed";
        case hipErrorArrayIsMapped              : return "hipErrorArrayIsMapped";
        case hipErrorAlreadyMapped              : return "hipErrorAlreadyMapped";
        case hipErrorNoBinaryForGpu             : return "hipErrorNoBinaryForGpu";
        case hipErrorAlreadyAcquired            : return "hipErrorAlreadyAcquired";
        case hipErrorNotMapped                  : return "hipErrorNotMapped";
        case hipErrorNotMappedAsArray           : return "hipErrorNotMappedAsArray";
        case hipErrorNotMappedAsPointer         : return "hipErrorNotMappedAsPointer";
        case hipErrorECCNotCorrectable          : return "hipErrorECCNotCorrectable";
        case hipErrorUnsupportedLimit           : return "hipErrorUnsupportedLimit";
        case hipErrorContextAlreadyInUse        : return "hipErrorContextAlreadyInUse";
        case hipErrorPeerAccessUnsupported      : return "hipErrorPeerAccessUnsupported";
        case hipErrorInvalidKernelFile          : return "hipErrorInvalidKernelFile";
        case hipErrorInvalidGraphicsContext     : return "hipErrorInvalidGraphicsContext";
        case hipErrorInvalidSource              : return "hipErrorInvalidSource";
        case hipErrorFileNotFound               : return "hipErrorFileNotFound";
        case hipErrorSharedObjectSymbolNotFound : return "hipErrorSharedObjectSymbolNotFound";
        case hipErrorSharedObjectInitFailed     : return "hipErrorSharedObjectInitFailed";
        case hipErrorOperatingSystem            : return "hipErrorOperatingSystem";
        case hipErrorSetOnActiveProcess         : return "hipErrorSetOnActiveProcess";
        case hipErrorInvalidHandle              : return "hipErrorInvalidHandle";
        case hipErrorNotFound                   : return "hipErrorNotFound";
        case hipErrorIllegalAddress             : return "hipErrorIllegalAddress";

        case hipErrorMissingConfiguration       : return "hipErrorMissingConfiguration";
        case hipErrorMemoryAllocation           : return "hipErrorMemoryAllocation";
        case hipErrorInitializationError        : return "hipErrorInitializationError";
        case hipErrorLaunchFailure              : return "hipErrorLaunchFailure";
        case hipErrorPriorLaunchFailure         : return "hipErrorPriorLaunchFailure";
        case hipErrorLaunchTimeOut              : return "hipErrorLaunchTimeOut";
        case hipErrorLaunchOutOfResources       : return "hipErrorLaunchOutOfResources";
        case hipErrorInvalidDeviceFunction      : return "hipErrorInvalidDeviceFunction";
        case hipErrorInvalidConfiguration       : return "hipErrorInvalidConfiguration";
        case hipErrorInvalidDevice              : return "hipErrorInvalidDevice";
        case hipErrorInvalidValue               : return "hipErrorInvalidValue";
        case hipErrorInvalidDevicePointer       : return "hipErrorInvalidDevicePointer";
        case hipErrorInvalidMemcpyDirection     : return "hipErrorInvalidMemcpyDirection";
        case hipErrorUnknown                    : return "hipErrorUnknown";
        case hipErrorInvalidResourceHandle      : return "hipErrorInvalidResourceHandle";
        case hipErrorNotReady                   : return "hipErrorNotReady";
        case hipErrorNoDevice                   : return "hipErrorNoDevice";
        case hipErrorPeerAccessAlreadyEnabled   : return "hipErrorPeerAccessAlreadyEnabled";

        case hipErrorPeerAccessNotEnabled       : return "hipErrorPeerAccessNotEnabled";
        case hipErrorRuntimeMemory              : return "hipErrorRuntimeMemory";
        case hipErrorRuntimeOther               : return "hipErrorRuntimeOther";
        case hipErrorHostMemoryAlreadyRegistered : return "hipErrorHostMemoryAlreadyRegistered";
        case hipErrorHostMemoryNotRegistered    : return "hipErrorHostMemoryNotRegistered";
        case hipErrorTbd                        : return "hipErrorTbd";
        default:
            error(NULL) << "Unknown amdgpu error " << err << "\n";
            return "<Unknown error>";
    }
}

WEAK halide_device_interface_impl_t amdgpu_device_interface_impl = {
    halide_use_jit_module,
    halide_release_jit_module,
    halide_amdgpu_device_malloc,
    halide_amdgpu_device_free,
    halide_amdgpu_device_sync,
    halide_amdgpu_device_release,
    halide_amdgpu_copy_to_host,
    halide_amdgpu_copy_to_device,
    halide_amdgpu_device_and_host_malloc,
    halide_amdgpu_device_and_host_free,
    halide_amdgpu_buffer_copy,
    halide_amdgpu_device_crop,
    halide_amdgpu_device_release_crop,
    halide_amdgpu_wrap_device_ptr,
    halide_amdgpu_detach_device_ptr,
};

WEAK halide_device_interface_t amdgpu_device_interface = {
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
    halide_device_release_crop,
    halide_device_wrap_native,
    halide_device_detach_native,
    &amdgpu_device_interface_impl
};

}}}} // namespace Halide::Runtime::Internal::Amdgpu
