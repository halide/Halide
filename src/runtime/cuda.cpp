#include "HalideRuntimeCuda.h"
#include "device_buffer_utils.h"
#include "device_interface.h"
#include "printer.h"
#include "mini_cuda.h"
#include "scoped_spin_lock.h"

#define INLINE inline __attribute__((always_inline))

namespace Halide { namespace Runtime { namespace Internal { namespace Cuda {

// Define the function pointers for the CUDA API.
#define CUDA_FN(ret, fn, args) WEAK ret (CUDAAPI *fn)args;
#define CUDA_FN_OPTIONAL(ret, fn, args) WEAK ret (CUDAAPI *fn)args;
#define CUDA_FN_3020(ret, fn, fn_3020, args) WEAK ret (CUDAAPI *fn)args;
#define CUDA_FN_4000(ret, fn, fn_4000, args) WEAK ret (CUDAAPI *fn)args;
#include "cuda_functions.h"
#undef CUDA_FN
#undef CUDA_FN_OPTIONAL
#undef CUDA_FN_3020
#undef CUDA_FN_4000

// The default implementation of halide_cuda_get_symbol attempts to load
// the CUDA shared library/DLL, and then get the symbol from it.
WEAK void *lib_cuda = NULL;
volatile int WEAK lib_cuda_lock = 0;

extern "C" WEAK void *halide_cuda_get_symbol(void *user_context, const char *name) {
    // Only try to load the library if we can't already get the symbol
    // from the library. Even if the library is NULL, the symbols may
    // already be available in the process.
    void *symbol = halide_get_library_symbol(lib_cuda, name);
    if (symbol) {
        return symbol;
    }

    const char *lib_names[] = {
#ifdef WINDOWS
        "nvcuda.dll",
#else
        "libcuda.so",
        "libcuda.dylib",
        "/Library/Frameworks/CUDA.framework/CUDA",
#endif
    };
    for (size_t i = 0; i < sizeof(lib_names) / sizeof(lib_names[0]); i++) {
        lib_cuda = halide_load_library(lib_names[i]);
        if (lib_cuda) {
            debug(user_context) << "    Loaded CUDA runtime library: " << lib_names[i] << "\n";
            break;
        }
    }

    return halide_get_library_symbol(lib_cuda, name);
}

template <typename T>
INLINE T get_cuda_symbol(void *user_context, const char *name, bool optional = false) {
    T s = (T)halide_cuda_get_symbol(user_context, name);
    if (!optional && !s) {
        error(user_context) << "CUDA API not found: " << name << "\n";
    }
    return s;
}

// Load a CUDA shared object/dll and get the CUDA API function pointers from it.
WEAK void load_libcuda(void *user_context) {
    debug(user_context) << "    load_libcuda (user_context: " << user_context << ")\n";
    halide_assert(user_context, cuInit == NULL);

    #define CUDA_FN(ret, fn, args) fn = get_cuda_symbol<ret (CUDAAPI *)args>(user_context, #fn);
    #define CUDA_FN_OPTIONAL(ret, fn, args) fn = get_cuda_symbol<ret (CUDAAPI *)args>(user_context, #fn, true);
    #define CUDA_FN_3020(ret, fn, fn_3020, args) fn = get_cuda_symbol<ret (CUDAAPI *)args>(user_context, #fn_3020);
    #define CUDA_FN_4000(ret, fn, fn_4000, args) fn = get_cuda_symbol<ret (CUDAAPI *)args>(user_context, #fn_4000);
    #include "cuda_functions.h"
    #undef CUDA_FN
    #undef CUDA_FN_OPTIONAL
    #undef CUDA_FN_3020
    #undef CUDA_FN_4000
}

// Call load_libcuda() if CUDA library has not been loaded.
// This function is thread safe.
// Note that initialization might fail. The caller can detect such failure by checking whether cuInit is NULL.
WEAK void ensure_libcuda_init(void *user_context) {
    ScopedSpinLock spinlock(&lib_cuda_lock);
    if (!cuInit) {
        load_libcuda(user_context);
    }
}

extern WEAK halide_device_interface_t cuda_device_interface;

WEAK const char *get_error_name(CUresult error);
WEAK CUresult create_cuda_context(void *user_context, CUcontext *ctx);

// A cuda context defined in this module with weak linkage
CUcontext WEAK context = 0;
// This spinlock protexts the above context variable.
volatile int WEAK context_lock = 0;

}}}} // namespace Halide::Runtime::Internal::Cuda

using namespace Halide::Runtime::Internal;
using namespace Halide::Runtime::Internal::Cuda;

extern "C" {

// The default implementation of halide_cuda_acquire_context uses the global
// pointers above, and serializes access with a spin lock.
// Overriding implementations of acquire/release must implement the following
// behavior:
// - halide_cuda_acquire_context should always store a valid context/command
//   queue in ctx/q, or return an error code.
// - A call to halide_cuda_acquire_context is followed by a matching call to
//   halide_cuda_release_context. halide_cuda_acquire_context should block while a
//   previous call (if any) has not yet been released via halide_cuda_release_context.
WEAK int halide_cuda_acquire_context(void *user_context, CUcontext *ctx, bool create = true) {
    // TODO: Should we use a more "assertive" assert? these asserts do
    // not block execution on failure.
    halide_assert(user_context, ctx != NULL);

    // If the context has not been initialized, initialize it now.
    halide_assert(user_context, &context != NULL);

    // Note that this null-check of the context is *not* locked with
    // respect to device_release, so we may get a non-null context
    // that's in the process of being destroyed. Things will go badly
    // in general if you call device_release while other Halide code
    // is running though.
    CUcontext local_val = context;
    if (local_val == NULL) {
        if (!create) {
            *ctx = NULL;
            return 0;
        }

        {
            ScopedSpinLock spinlock(&context_lock);
            local_val = context;
            if (local_val == NULL) {
                CUresult error = create_cuda_context(user_context, &local_val);
                if (error != CUDA_SUCCESS) {
                    return error;
                }
            }
            // Normally in double-checked locking you need a release
            // fence here that synchronizes with an acquire fence
            // above to ensure context is fully constructed before
            // assigning to the global, but there's no way that
            // create_cuda_context can access the "context" global, so
            // we should be OK just storing to it here.
            context = local_val;
        }  // spinlock
    }

    *ctx = local_val;
    return 0;
}

WEAK int halide_cuda_release_context(void *user_context) {
    return 0;
}

// Return the stream to use for executing kernels and synchronization. Only called
// for versions of cuda which support streams. Default is to use the main stream
// for the context (NULL stream). The context is passed in for convenience, but
// any sort of scoping must be handled by that of the
// halide_cuda_acquire_context/halide_cuda_release_context pair, not this call.
WEAK int halide_cuda_get_stream(void *user_context, CUcontext ctx, CUstream *stream) {
    // There are two default streams we could use. stream 0 is fully
    // synchronous. stream 2 gives a separate non-blocking stream per
    // thread.
    *stream = 0;
    return 0;
}

} // extern "C"

namespace Halide { namespace Runtime { namespace Internal { namespace Cuda {

// Helper object to acquire and release the cuda context.
class Context {
    void *user_context;

public:
    CUcontext context;
    int error;

    // Constructor sets 'error' if any occurs.
    INLINE Context(void *user_context) : user_context(user_context),
                                         context(NULL),
                                         error(CUDA_SUCCESS) {
#ifdef DEBUG_RUNTIME
        halide_start_clock(user_context);
#endif
        error = halide_cuda_acquire_context(user_context, &context);
        if (error != 0) {
            return;
        }

        // The default acquire_context loads libcuda as a
        // side-effect. However, if acquire_context has been
        // overridden, we may still need to load libcuda
        ensure_libcuda_init(user_context);

        halide_assert(user_context, context != NULL);
        halide_assert(user_context, cuInit != NULL);

        error = cuCtxPushCurrent(context);
    }

    INLINE ~Context() {
        if (error == 0) {
            CUcontext old;
            cuCtxPopCurrent(&old);
            halide_cuda_release_context(user_context);
        }
    }
};

// Halide allocates a device API controlled pointer slot as part of
// each compiled module. The slot is used to store information to
// avoid having to reload/recompile kernel code on each call into a
// Halide filter. The cuda runtime uses this pointer to maintain a
// linked list of contexts into which the module has been loaded.
//
// A global list of all registered filters is also kept so all modules
// loaded on a given context can be unloaded and removed from the list
// when halide_device_release is called on a specific context.
//
// The registered_filters struct is not freed as it is pointed to by the
// Halide generated code. The module_state structs are freed.

struct module_state {
    CUcontext context;
    CUmodule module;
    module_state *next;
};

struct registered_filters {
    module_state *modules;
    registered_filters *next;
};
WEAK registered_filters *filters_list = NULL;
// This spinlock protects the above filters_list.
volatile int WEAK filters_list_lock = 0;

WEAK module_state *find_module_for_context(const registered_filters *filters, CUcontext ctx) {
    module_state *modules = filters->modules;
    while (modules != NULL) {
        if (modules->context == ctx) {
            return modules;
        }
        modules = modules->next;
    }
    return NULL;
}

WEAK CUresult create_cuda_context(void *user_context, CUcontext *ctx) {
    // Initialize CUDA
    ensure_libcuda_init(user_context);
    if (!cuInit) {
        error(user_context) << "Could not find cuda system libraries";
        return CUDA_ERROR_FILE_NOT_FOUND;
    }

    CUresult err = cuInit(0);
    if (err != CUDA_SUCCESS) {
        error(user_context) << "CUDA: cuInit failed: "
                            << get_error_name(err);
        return err;
    }

    // Make sure we have a device
    int deviceCount = 0;
    err = cuDeviceGetCount(&deviceCount);
    if (err != CUDA_SUCCESS) {
        error(user_context) << "CUDA: cuGetDeviceCount failed: "
                            << get_error_name(err);
        return err;
    }

    if (deviceCount <= 0) {
        halide_error(user_context, "CUDA: No devices available");
        return CUDA_ERROR_NO_DEVICE;
    }

    int device = halide_get_gpu_device(user_context);
    if (device == -1 && deviceCount == 1) {
        device = 0;
    } else if (device == -1) {
        debug(user_context) << "CUDA: Multiple CUDA devices detected. Selecting the one with the most cores.\n";
        int best_core_count = 0;
        for (int i = 0; i < deviceCount; i++) {
            CUdevice dev;
            CUresult status = cuDeviceGet(&dev, i);
            if (status != CUDA_SUCCESS) {
                debug(user_context) << "      Failed to get device " << i << "\n";
                continue;
            }
            int core_count = 0;
            status = cuDeviceGetAttribute(&core_count, CU_DEVICE_ATTRIBUTE_MULTIPROCESSOR_COUNT, dev);
            debug(user_context) << "      Device " << i << " has " << core_count << " cores\n";
            if (status != CUDA_SUCCESS) {
                continue;
            }
            if (core_count >= best_core_count) {
                device = i;
                best_core_count = core_count;
            }
        }
    }

    // Get device
    CUdevice dev;
    CUresult status = cuDeviceGet(&dev, device);
    if (status != CUDA_SUCCESS) {
        halide_error(user_context, "CUDA: Failed to get device\n");
        return status;
    }

    debug(user_context) <<  "    Got device " << dev << "\n";

    // Dump device attributes
    #ifdef DEBUG_RUNTIME
    {
        char name[256];
        name[0] = 0;
        err = cuDeviceGetName(name, 256, dev);
        debug(user_context) << "      " << name << "\n";

        if (err != CUDA_SUCCESS) {
            error(user_context) << "CUDA: cuDeviceGetName failed: "
                                << get_error_name(err);
            return err;
        }

        size_t memory = 0;
        err = cuDeviceTotalMem(&memory, dev);
        debug(user_context) << "      total memory: " << (int)(memory >> 20) << " MB\n";

        if (err != CUDA_SUCCESS) {
            error(user_context) << "CUDA: cuDeviceTotalMem failed: "
                                << get_error_name(err);
            return err;
        }

        // Declare variables for other state we want to query.
        int max_threads_per_block = 0, warp_size = 0, num_cores = 0;
        int max_block_size[] = {0, 0, 0};
        int max_grid_size[] = {0, 0, 0};
        int max_shared_mem = 0, max_constant_mem = 0;
        int cc_major = 0, cc_minor = 0;

        struct {int *dst; CUdevice_attribute attr;} attrs[] = {
            {&max_threads_per_block, CU_DEVICE_ATTRIBUTE_MAX_THREADS_PER_BLOCK},
            {&warp_size,             CU_DEVICE_ATTRIBUTE_WARP_SIZE},
            {&num_cores,             CU_DEVICE_ATTRIBUTE_MULTIPROCESSOR_COUNT},
            {&max_block_size[0],     CU_DEVICE_ATTRIBUTE_MAX_BLOCK_DIM_X},
            {&max_block_size[1],     CU_DEVICE_ATTRIBUTE_MAX_BLOCK_DIM_Y},
            {&max_block_size[2],     CU_DEVICE_ATTRIBUTE_MAX_BLOCK_DIM_Z},
            {&max_grid_size[0],      CU_DEVICE_ATTRIBUTE_MAX_GRID_DIM_X},
            {&max_grid_size[1],      CU_DEVICE_ATTRIBUTE_MAX_GRID_DIM_Y},
            {&max_grid_size[2],      CU_DEVICE_ATTRIBUTE_MAX_GRID_DIM_Z},
            {&max_shared_mem,        CU_DEVICE_ATTRIBUTE_SHARED_MEMORY_PER_BLOCK},
            {&max_constant_mem,      CU_DEVICE_ATTRIBUTE_TOTAL_CONSTANT_MEMORY},
            {&cc_major,              CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR},
            {&cc_minor,              CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR},
            {NULL,                   CU_DEVICE_ATTRIBUTE_MAX}};

        // Do all the queries.
        for (int i = 0; attrs[i].dst; i++) {
            err = cuDeviceGetAttribute(attrs[i].dst, attrs[i].attr, dev);
            if (err != CUDA_SUCCESS) {
                error(user_context)
                    << "CUDA: cuDeviceGetAttribute failed ("
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
            << "      cuda cores: " << num_cores << " x " << threads_per_core << " = " << threads_per_core << "\n";
    }
    #endif

    // Create context
    debug(user_context) <<  "    cuCtxCreate " << dev << " -> ";
    err = cuCtxCreate(ctx, 0, dev);
    if (err != CUDA_SUCCESS) {
        debug(user_context) << get_error_name(err) << "\n";
        error(user_context) << "CUDA: cuCtxCreate failed: "
                            << get_error_name(err);
        return err;
    } else {
        unsigned int version = 0;
        cuCtxGetApiVersion(*ctx, &version);
        debug(user_context) << *ctx << "(" << version << ")\n";
    }
    // Creation automatically pushes the context, but we'll pop to allow the caller
    // to decide when to push.
    CUcontext dummy;
    err = cuCtxPopCurrent(&dummy);
    if (err != CUDA_SUCCESS) {
      error(user_context) << "CUDA: cuCtxPopCurrent failed: "
                          << get_error_name(err);
      return err;
    }

    return CUDA_SUCCESS;
}

// This feature may be useful during CUDA backend or runtime
// development. It does not seem to find many errors in general Halide
// use and causes false positives in at least one environment, where
// it prevents using debug mode with cuda.
#define ENABLE_POINTER_VALIDATION 0

WEAK bool validate_device_pointer(void *user_context, halide_buffer_t* buf, size_t size=0) {
// The technique using cuPointerGetAttribute and CU_POINTER_ATTRIBUTE_CONTEXT
// requires unified virtual addressing is enabled and that is not the case
// for 32-bit processes on Mac OS X. So for now, as a total hack, just return true
// in 32-bit. This could of course be wrong the other way for cards that only
// support 32-bit addressing in 64-bit processes, but I expect those cards do not
// support unified addressing at all.
// TODO: figure out a way to validate pointers in all cases if strictly necessary.
#if defined(BITS_32) || !ENABLE_POINTER_VALIDATION
    return true;
#else
    if (buf->device == 0)
        return true;

    CUdeviceptr dev_ptr = (CUdeviceptr)buf->device;

    CUcontext ctx;
    CUresult result = cuPointerGetAttribute(&ctx, CU_POINTER_ATTRIBUTE_CONTEXT, dev_ptr);
    if (result != CUDA_SUCCESS) {
        error(user_context) << "Bad device pointer " << (void *)dev_ptr
                            << ": cuPointerGetAttribute returned "
                            << get_error_name(result);
        return false;
    }
    return true;
#endif
}

}}}} // namespace Halide::Runtime::Internal

extern "C" {
WEAK int halide_cuda_initialize_kernels(void *user_context, void **state_ptr, const char* ptx_src, int size) {
    debug(user_context) << "CUDA: halide_cuda_initialize_kernels (user_context: " << user_context
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
            debug(user_context) <<  "    cuModuleLoadData " << (void *)ptx_src << ", " << size << " -> ";

            CUjit_option options[] = { CU_JIT_MAX_REGISTERS };
            unsigned int max_regs_per_thread = 64;

            // A hack to enable control over max register count for
            // testing. This should be surfaced in the schedule somehow
            // instead.
            char *regs = getenv("HL_CUDA_JIT_MAX_REGISTERS");
            if (regs) {
                max_regs_per_thread = atoi(regs);
            }
            void *optionValues[] = { (void*)(uintptr_t) max_regs_per_thread };
            CUresult err = cuModuleLoadDataEx(&loaded_module->module, ptx_src, 1, options, optionValues);

            if (err != CUDA_SUCCESS) {
                free(loaded_module);
                error(user_context) << "CUDA: cuModuleLoadData failed: "
                                    << get_error_name(err);
                return err;
            } else {
                debug(user_context) << (void *)(loaded_module->module) << "\n";
            }
            loaded_module->context = ctx.context;
            loaded_module->next = (*filters)->modules;
            (*filters)->modules = loaded_module;
        }
    }  // spinlock

    #ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
    #endif

    return 0;
}

WEAK int halide_cuda_device_free(void *user_context, halide_buffer_t* buf) {
    // halide_device_free, at present, can be exposed to clients and they
    // should be allowed to call halide_device_free on any halide_buffer_t
    // including ones that have never been used with a GPU.
    if (buf->device == 0) {
        return 0;
    }

    CUdeviceptr dev_ptr = (CUdeviceptr)buf->device;

    debug(user_context)
        <<  "CUDA: halide_cuda_device_free (user_context: " << user_context
        << ", buf: " << buf << ")\n";

    Context ctx(user_context);
    if (ctx.error != CUDA_SUCCESS)
        return ctx.error;

    #ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
    #endif

    halide_assert(user_context, validate_device_pointer(user_context, buf));

    debug(user_context) <<  "    cuMemFree " << (void *)(dev_ptr) << "\n";
    CUresult err = cuMemFree(dev_ptr);
    // If cuMemFree fails, it isn't likely to succeed later, so just drop
    // the reference.
    buf->device_interface->impl->release_module();
    buf->device_interface = NULL;
    buf->device = 0;
    if (err != CUDA_SUCCESS) {
        // We may be called as a destructor, so don't raise an error here.
        return err;
    }

    #ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
    #endif

    return 0;
}

WEAK int halide_cuda_device_release(void *user_context) {
    debug(user_context)
        << "CUDA: halide_cuda_device_release (user_context: " <<  user_context << ")\n";

    // If we haven't even loaded libcuda, don't load it just to quit.
    if (!lib_cuda) {
        return 0;
    }

    int err;
    CUcontext ctx;
    err = halide_cuda_acquire_context(user_context, &ctx, false);
    if (err != CUDA_SUCCESS) {
        return err;
    }

    if (ctx) {
        // It's possible that this is being called from the destructor of
        // a static variable, in which case the driver may already be
        // shutting down.
        err = cuCtxPushCurrent(ctx);
        if (err != CUDA_SUCCESS) {
            err = cuCtxSynchronize();
        }
        halide_assert(user_context, err == CUDA_SUCCESS || err == CUDA_ERROR_DEINITIALIZED);

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
                        debug(user_context) << "    cuModuleUnload " << loaded_module->module << "\n";
                        err = cuModuleUnload(loaded_module->module);
                        halide_assert(user_context, err == CUDA_SUCCESS || err == CUDA_ERROR_DEINITIALIZED);
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

        CUcontext old_ctx;
        cuCtxPopCurrent(&old_ctx);

        // Only destroy the context if we own it

        {
            ScopedSpinLock spinlock(&context_lock);

            if (ctx == context) {
                debug(user_context) << "    cuCtxDestroy " << context << "\n";
                err = cuProfilerStop();
                err = cuCtxDestroy(context);
                halide_assert(user_context, err == CUDA_SUCCESS || err == CUDA_ERROR_DEINITIALIZED);
                context = NULL;
            }
        }  // spinlock
    }

    halide_cuda_release_context(user_context);

    return 0;
}

WEAK int halide_cuda_device_malloc(void *user_context, halide_buffer_t *buf) {
    debug(user_context)
        << "CUDA: halide_cuda_device_malloc (user_context: " << user_context
        << ", buf: " << buf << ")\n";

    Context ctx(user_context);
    if (ctx.error != CUDA_SUCCESS) {
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

    CUdeviceptr p;
    debug(user_context) << "    cuMemAlloc " << (uint64_t)size << " -> ";
    CUresult err = cuMemAlloc(&p, size);
    if (err != CUDA_SUCCESS) {
        debug(user_context) << get_error_name(err) << "\n";
        error(user_context) << "CUDA: cuMemAlloc failed: "
                            << get_error_name(err);
        return err;
    } else {
        debug(user_context) << (void *)p << "\n";
    }
    halide_assert(user_context, p);
    buf->device = p;
    buf->device_interface = &cuda_device_interface;
    buf->device_interface->impl->use_module();

    #ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
    #endif

    return 0;
}

namespace {
WEAK int cuda_do_multidimensional_copy(void *user_context, const device_copy &c,
                                       uint64_t src, uint64_t dst, int d, bool from_host, bool to_host) {
    if (d > MAX_COPY_DIMS) {
        error(user_context) << "Buffer has too many dimensions to copy to/from GPU\n";
        return -1;
    } else if (d == 0) {
        CUresult err = CUDA_SUCCESS;
        const char *copy_name;
        debug(user_context) << "    from " << (from_host ? "host" : "device")
                            << " to " << (to_host ? "host" : "device") << ", "
                            << (void *)src << " -> " << (void *)dst << ", " << c.chunk_size << " bytes\n";
        if (!from_host && to_host) {
            debug(user_context) << "cuMemcpyDtoH(" << (void *)dst << ", " << (void *)src << ", " << c.chunk_size <<")\n";
            err = cuMemcpyDtoH((void *)dst, (CUdeviceptr)src, c.chunk_size);
        } else if (from_host && !to_host) {
            debug(user_context) << "cuMemcpyHtoD(" << (void *)dst << ", " << (void *)src << ", " << c.chunk_size <<")\n";
            err = cuMemcpyHtoD((CUdeviceptr)dst, (void *)src, c.chunk_size);
        } else if (!from_host && !to_host) {
            debug(user_context) << "cuMemcpyDtoD(" << (void *)dst << ", " << (void *)src << ", " << c.chunk_size <<")\n";
            err = cuMemcpyDtoD((CUdeviceptr)dst, (CUdeviceptr)src, c.chunk_size);
        } else if (dst != src) {
            debug(user_context) << "memcpy(" << (void *)dst << ", " << (void *)src << ", " << c.chunk_size <<")\n";
            // Could reach here if a user called directly into the
            // cuda API for a device->host copy on a source buffer
            // with device_dirty = false.
            memcpy((void *)dst, (void *)src, c.chunk_size);
        }
        if (err != CUDA_SUCCESS) {
            error(user_context) << "CUDA: " << copy_name << " failed: " << get_error_name(err);
            return (int)err;
        }
    } else {
        ssize_t src_off = 0, dst_off = 0;
        for (int i = 0; i < (int)c.extent[d-1]; i++) {
            int err = cuda_do_multidimensional_copy(user_context, c, src + src_off, dst + dst_off, d - 1, from_host, to_host);
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

WEAK int halide_cuda_buffer_copy(void *user_context, struct halide_buffer_t *src,
                                 const struct halide_device_interface_t *dst_device_interface,
                                 struct halide_buffer_t *dst) {
    // We only handle copies to cuda or to host
    halide_assert(user_context, dst_device_interface == NULL ||
                  dst_device_interface == &cuda_device_interface);

    if ((src->device_dirty() || src->host == NULL) &&
        src->device_interface != &cuda_device_interface) {
        halide_assert(user_context, dst_device_interface == &cuda_device_interface);
        // This is handled at the higher level.
        return halide_error_code_incompatible_device_interface;
    }

    bool from_host = (src->device_interface != &cuda_device_interface) ||
                     (src->device == 0) ||
                     (src->host_dirty() && src->host != NULL);
    bool to_host = !dst_device_interface;

    halide_assert(user_context, from_host || src->device);
    halide_assert(user_context, to_host || dst->device);

    device_copy c = make_buffer_copy(src, from_host, dst, to_host);

    int err = 0;
    {
        Context ctx(user_context);
        if (ctx.error != CUDA_SUCCESS) {
            return ctx.error;
        }

        debug(user_context)
            << "CUDA: halide_cuda_buffer_copy (user_context: " << user_context
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

        err = cuda_do_multidimensional_copy(user_context, c, c.src + c.src_begin, c.dst, dst->dimensions, from_host, to_host);

        #ifdef DEBUG_RUNTIME
        uint64_t t_after = halide_current_time_ns(user_context);
        debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
        #endif
    }

    return err;
}

namespace {

WEAK int cuda_device_crop_from_offset(const struct halide_buffer_t *src,
                                      int64_t offset,
                                      struct halide_buffer_t *dst) {
    dst->device = src->device + offset;
    dst->device_interface = src->device_interface;
    dst->set_device_dirty(src->device_dirty());
    return 0;
}

}  // namespace

WEAK int halide_cuda_device_crop(void *user_context, const struct halide_buffer_t *src,
                                 struct halide_buffer_t *dst) {
    debug(user_context)
        << "CUDA: halide_cuda_device_crop (user_context: " << user_context
        << ", src: " << src << ", dst: " << dst << ")\n";

    // Pointer arithmetic works fine.
    const int64_t offset = calc_device_crop_byte_offset(src, dst);
    return cuda_device_crop_from_offset(src, offset, dst);
}

WEAK int halide_cuda_device_slice(void *user_context, const struct halide_buffer_t *src,
                                  int slice_dim, int slice_pos,
                                  struct halide_buffer_t *dst) {
    debug(user_context)
        << "CUDA: halide_cuda_device_slice (user_context: " << user_context
        << ", src: " << src << ", slice_dim " << slice_dim << ", slice_pos "
        << slice_pos << ", dst: " << dst << ")\n";

    // Pointer arithmetic works fine.
    const int64_t offset = calc_device_slice_byte_offset(src, slice_dim, slice_pos);
    return cuda_device_crop_from_offset(src, offset, dst);
}

WEAK int halide_cuda_device_release_crop(void *user_context, struct halide_buffer_t *dst) {
    debug(user_context)
        << "CUDA: halide_cuda_release_crop (user_context: " << user_context
        << ", dst: " << dst << ")\n";
    return 0;
}

WEAK int halide_cuda_copy_to_device(void *user_context, halide_buffer_t* buf) {
    return halide_cuda_buffer_copy(user_context, buf, &cuda_device_interface, buf);
}

WEAK int halide_cuda_copy_to_host(void *user_context, halide_buffer_t* buf) {
    return halide_cuda_buffer_copy(user_context, buf, NULL, buf);
}

// Used to generate correct timings when tracing
WEAK int halide_cuda_device_sync(void *user_context, struct halide_buffer_t *) {
    debug(user_context)
        << "CUDA: halide_cuda_device_sync (user_context: " << user_context << ")\n";

    Context ctx(user_context);
    if (ctx.error != CUDA_SUCCESS) {
        return ctx.error;
    }

    #ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
    #endif

    CUresult err;
    if (cuStreamSynchronize != NULL) {
        CUstream stream;
        int result = halide_cuda_get_stream(user_context, ctx.context, &stream);
        if (result != 0) {
            error(user_context) << "CUDA: In halide_cuda_device_sync, halide_cuda_get_stream returned " << result << "\n";
        }
        err = cuStreamSynchronize(stream);
    } else {
       err = cuCtxSynchronize();
    }
    if (err != CUDA_SUCCESS) {
        error(user_context) << "CUDA: cuCtxSynchronize failed: "
                            << get_error_name(err);
        return err;
    }

    #ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
    #endif

    return 0;
}

WEAK int halide_cuda_run(void *user_context,
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

    debug(user_context) << "CUDA: halide_cuda_run ("
                        << "user_context: " << user_context << ", "
                        << "entry: " << entry_name << ", "
                        << "blocks: " << blocksX << "x" << blocksY << "x" << blocksZ << ", "
                        << "threads: " << threadsX << "x" << threadsY << "x" << threadsZ << ", "
                        << "shmem: " << shared_mem_bytes << "\n";

    CUresult err;
    Context ctx(user_context);
    if (ctx.error != CUDA_SUCCESS) {
        return ctx.error;
    }

    debug(user_context) << "Got context.\n";

    #ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
    #endif

    halide_assert(user_context, state_ptr);
    module_state *loaded_module = find_module_for_context((registered_filters *)state_ptr, ctx.context);
    halide_assert(user_context, loaded_module != NULL);
    CUmodule mod = loaded_module->module;
    debug(user_context) << "Got module " << mod << "\n";
    halide_assert(user_context, mod);
    CUfunction f;
    err = cuModuleGetFunction(&f, mod, entry_name);
    debug(user_context) << "Got function " << f << "\n";
    if (err != CUDA_SUCCESS) {
        error(user_context) << "CUDA: cuModuleGetFunction failed: "
                            << get_error_name(err);
        return err;
    }

    size_t num_args = 0;
    while (arg_sizes[num_args] != 0) {
        debug(user_context) << "    halide_cuda_run " << (int)num_args
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

            // We might be passing an argument to a constant
            // buffer. If so, we should copy the argument to the
            // constant buffer, and pass that to the kernel instead.
            char constant_name_buf[1024];
            stringstream constant_name(user_context, constant_name_buf);
            constant_name << entry_name << "_const_arg" << (int)i;
            CUdeviceptr constant_ptr = NULL;
            size_t constant_size = 0;
            debug(user_context) << "cuModuleGetGlobal(" << constant_name.str() << ") -> ";
            err = cuModuleGetGlobal(&constant_ptr, &constant_size, mod, constant_name.str());
            debug(user_context) << "err=" << err << ", ptr=" << constant_ptr << ", size=" << (int)constant_size;
            if (err == CUDA_SUCCESS) {
                debug(user_context) << "    halide_cuda_run found constant for argument " << (int)i
                                    << " (" << constant_name.str() << " of size " << (int)constant_size << " bytes...";
                err = cuMemcpyDtoD(constant_ptr, dev_handles[i], constant_size);
                if (err != CUDA_SUCCESS) {
                    error(user_context) << "CUDA: cuMemcpyDtoD failed: "
                                        << get_error_name(err);
                    return err;
                }
                dev_handles[i] = constant_ptr;
            }
            translated_args[i] = &(dev_handles[i]);
            debug(user_context) << "    halide_cuda_run translated arg" << (int)i
                                << " [" << (*((void **)translated_args[i])) << " ...]\n";
        } else {
            translated_args[i] = args[i];
        }
    }

    CUstream stream = NULL;
    // We use whether this routine was defined in the cuda driver library
    // as a test for streams support in the cuda implementation.
    if (cuStreamSynchronize != NULL) {
        int result = halide_cuda_get_stream(user_context, ctx.context, &stream);
        if (result != 0) {
            error(user_context) << "CUDA: In halide_cuda_run, halide_cuda_get_stream returned " << result << "\n";
        }
    }

    err = cuLaunchKernel(f,
                         blocksX,  blocksY,  blocksZ,
                         threadsX, threadsY, threadsZ,
                         shared_mem_bytes,
                         stream,
                         translated_args,
                         NULL);
    free(dev_handles);
    free(translated_args);
    if (err != CUDA_SUCCESS) {
        error(user_context) << "CUDA: cuLaunchKernel failed: "
                            << get_error_name(err);
        return err;
    }

    #ifdef DEBUG_RUNTIME
    err = cuCtxSynchronize();
    if (err != CUDA_SUCCESS) {
        error(user_context) << "CUDA: cuCtxSynchronize failed: "
                            << get_error_name(err);
        return err;
    }
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
    #endif
    return 0;
}

WEAK int halide_cuda_device_and_host_malloc(void *user_context, struct halide_buffer_t *buf) {
    return halide_default_device_and_host_malloc(user_context, buf, &cuda_device_interface);
}

WEAK int halide_cuda_device_and_host_free(void *user_context, struct halide_buffer_t *buf) {
    return halide_default_device_and_host_free(user_context, buf, &cuda_device_interface);
}

WEAK int halide_cuda_wrap_device_ptr(void *user_context, struct halide_buffer_t *buf, uint64_t device_ptr) {
    halide_assert(user_context, buf->device == 0);
    if (buf->device != 0) {
        return -2;
    }
    buf->device = device_ptr;
    buf->device_interface = &cuda_device_interface;
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

WEAK int halide_cuda_detach_device_ptr(void *user_context, struct halide_buffer_t *buf) {
    if (buf->device == NULL) {
        return 0;
    }
    halide_assert(user_context, buf->device_interface == &cuda_device_interface);
    buf->device_interface->impl->release_module();
    buf->device = 0;
    buf->device_interface = NULL;
    return 0;
}

WEAK uintptr_t halide_cuda_get_device_ptr(void *user_context, struct halide_buffer_t *buf) {
    if (buf->device == NULL) {
        return 0;
    }
    halide_assert(user_context, buf->device_interface == &cuda_device_interface);
    return (uintptr_t)buf->device;
}

WEAK const halide_device_interface_t *halide_cuda_device_interface() {
    return &cuda_device_interface;
}

WEAK int halide_cuda_compute_capability(void *user_context, int *major, int *minor) {
    if (!lib_cuda) {
        // If cuda can't be found, we want to return 0, 0 and it's not
        // considered an error. So we should be very careful about
        // looking for libcuda without tripping any errors in the rest
        // of this runtime.
        void *sym = halide_cuda_get_symbol(user_context, "cuInit");
        if (!sym) {
            *major = *minor = 0;
            return 0;
        }
    }

    {
        Context ctx(user_context);
        if (ctx.error != 0) {
            return ctx.error;
        }

        CUresult err;

        CUdevice dev;
        err = cuCtxGetDevice(&dev);
        if (err != CUDA_SUCCESS) {
            error(user_context)
                << "CUDA: cuCtxGetDevice failed ("
                << Halide::Runtime::Internal::Cuda::get_error_name(err)
                << ")";
            return err;
        }

        err = cuDeviceGetAttribute(major, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR, dev);
        if (err == CUDA_SUCCESS) {
            err = cuDeviceGetAttribute(minor, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR, dev);
        }

        if (err != CUDA_SUCCESS) {
            error(user_context)
                << "CUDA: cuDeviceGetAttribute failed ("
                << Halide::Runtime::Internal::Cuda::get_error_name(err)
                << ")";
            return err;
        }
    }

    return 0;
}

namespace {
__attribute__((destructor))
WEAK void halide_cuda_cleanup() {
    halide_cuda_device_release(NULL);
}
}

} // extern "C" linkage

namespace Halide { namespace Runtime { namespace Internal { namespace Cuda {

WEAK const char *get_error_name(CUresult err) {
    switch(err) {
    case CUDA_SUCCESS: return "CUDA_SUCCESS";
    case CUDA_ERROR_INVALID_VALUE: return "CUDA_ERROR_INVALID_VALUE";
    case CUDA_ERROR_OUT_OF_MEMORY: return "CUDA_ERROR_OUT_OF_MEMORY";
    case CUDA_ERROR_NOT_INITIALIZED: return "CUDA_ERROR_NOT_INITIALIZED";
    case CUDA_ERROR_DEINITIALIZED: return "CUDA_ERROR_DEINITIALIZED";
    case CUDA_ERROR_PROFILER_DISABLED: return "CUDA_ERROR_PROFILER_DISABLED";
    case CUDA_ERROR_PROFILER_NOT_INITIALIZED: return "CUDA_ERROR_PROFILER_NOT_INITIALIZED";
    case CUDA_ERROR_PROFILER_ALREADY_STARTED: return "CUDA_ERROR_PROFILER_ALREADY_STARTED";
    case CUDA_ERROR_PROFILER_ALREADY_STOPPED: return "CUDA_ERROR_PROFILER_ALREADY_STOPPED";
    case CUDA_ERROR_NO_DEVICE: return "CUDA_ERROR_NO_DEVICE";
    case CUDA_ERROR_INVALID_DEVICE: return "CUDA_ERROR_INVALID_DEVICE";
    case CUDA_ERROR_INVALID_IMAGE: return "CUDA_ERROR_INVALID_IMAGE";
    case CUDA_ERROR_INVALID_CONTEXT: return "CUDA_ERROR_INVALID_CONTEXT";
    case CUDA_ERROR_CONTEXT_ALREADY_CURRENT: return "CUDA_ERROR_CONTEXT_ALREADY_CURRENT";
    case CUDA_ERROR_MAP_FAILED: return "CUDA_ERROR_MAP_FAILED";
    case CUDA_ERROR_UNMAP_FAILED: return "CUDA_ERROR_UNMAP_FAILED";
    case CUDA_ERROR_ARRAY_IS_MAPPED: return "CUDA_ERROR_ARRAY_IS_MAPPED";
    case CUDA_ERROR_ALREADY_MAPPED: return "CUDA_ERROR_ALREADY_MAPPED";
    case CUDA_ERROR_NO_BINARY_FOR_GPU: return "CUDA_ERROR_NO_BINARY_FOR_GPU";
    case CUDA_ERROR_ALREADY_ACQUIRED: return "CUDA_ERROR_ALREADY_ACQUIRED";
    case CUDA_ERROR_NOT_MAPPED: return "CUDA_ERROR_NOT_MAPPED";
    case CUDA_ERROR_NOT_MAPPED_AS_ARRAY: return "CUDA_ERROR_NOT_MAPPED_AS_ARRAY";
    case CUDA_ERROR_NOT_MAPPED_AS_POINTER: return "CUDA_ERROR_NOT_MAPPED_AS_POINTER";
    case CUDA_ERROR_ECC_UNCORRECTABLE: return "CUDA_ERROR_ECC_UNCORRECTABLE";
    case CUDA_ERROR_UNSUPPORTED_LIMIT: return "CUDA_ERROR_UNSUPPORTED_LIMIT";
    case CUDA_ERROR_CONTEXT_ALREADY_IN_USE: return "CUDA_ERROR_CONTEXT_ALREADY_IN_USE";
    case CUDA_ERROR_PEER_ACCESS_UNSUPPORTED: return "CUDA_ERROR_PEER_ACCESS_UNSUPPORTED";
    case CUDA_ERROR_INVALID_PTX: return "CUDA_ERROR_INVALID_PTX";
    case CUDA_ERROR_INVALID_GRAPHICS_CONTEXT: return "CUDA_ERROR_INVALID_GRAPHICS_CONTEXT";
    case CUDA_ERROR_NVLINK_UNCORRECTABLE: return "CUDA_ERROR_NVLINK_UNCORRECTABLE";
    case CUDA_ERROR_JIT_COMPILER_NOT_FOUND: return "CUDA_ERROR_JIT_COMPILER_NOT_FOUND";
    case CUDA_ERROR_INVALID_SOURCE: return "CUDA_ERROR_INVALID_SOURCE";
    case CUDA_ERROR_FILE_NOT_FOUND: return "CUDA_ERROR_FILE_NOT_FOUND";
    case CUDA_ERROR_SHARED_OBJECT_SYMBOL_NOT_FOUND: return "CUDA_ERROR_SHARED_OBJECT_SYMBOL_NOT_FOUND";
    case CUDA_ERROR_SHARED_OBJECT_INIT_FAILED: return "CUDA_ERROR_SHARED_OBJECT_INIT_FAILED";
    case CUDA_ERROR_OPERATING_SYSTEM: return "CUDA_ERROR_OPERATING_SYSTEM";
    case CUDA_ERROR_INVALID_HANDLE: return "CUDA_ERROR_INVALID_HANDLE";
    case CUDA_ERROR_NOT_FOUND: return "CUDA_ERROR_NOT_FOUND";
    case CUDA_ERROR_NOT_READY: return "CUDA_ERROR_NOT_READY";
    case CUDA_ERROR_ILLEGAL_ADDRESS: return "CUDA_ERROR_ILLEGAL_ADDRESS";
    case CUDA_ERROR_LAUNCH_OUT_OF_RESOURCES: return "CUDA_ERROR_LAUNCH_OUT_OF_RESOURCES";
    case CUDA_ERROR_LAUNCH_TIMEOUT: return "CUDA_ERROR_LAUNCH_TIMEOUT";
    case CUDA_ERROR_LAUNCH_INCOMPATIBLE_TEXTURING: return "CUDA_ERROR_LAUNCH_INCOMPATIBLE_TEXTURING";
    case CUDA_ERROR_PEER_ACCESS_ALREADY_ENABLED: return "CUDA_ERROR_PEER_ACCESS_ALREADY_ENABLED";
    case CUDA_ERROR_PEER_ACCESS_NOT_ENABLED: return "CUDA_ERROR_PEER_ACCESS_NOT_ENABLED";
    case CUDA_ERROR_PRIMARY_CONTEXT_ACTIVE: return "CUDA_ERROR_PRIMARY_CONTEXT_ACTIVE";
    case CUDA_ERROR_CONTEXT_IS_DESTROYED: return "CUDA_ERROR_CONTEXT_IS_DESTROYED";
    // A trap instruction produces the below error, which is how we codegen asserts on GPU
    case CUDA_ERROR_ILLEGAL_INSTRUCTION:
        return "Illegal instruction or Halide assertion failure inside kernel";
    case CUDA_ERROR_MISALIGNED_ADDRESS: return "CUDA_ERROR_MISALIGNED_ADDRESS";
    case CUDA_ERROR_INVALID_ADDRESS_SPACE: return "CUDA_ERROR_INVALID_ADDRESS_SPACE";
    case CUDA_ERROR_INVALID_PC: return "CUDA_ERROR_INVALID_PC";
    case CUDA_ERROR_LAUNCH_FAILED: return "CUDA_ERROR_LAUNCH_FAILED";
    case CUDA_ERROR_NOT_PERMITTED: return "CUDA_ERROR_NOT_PERMITTED";
    case CUDA_ERROR_NOT_SUPPORTED: return "CUDA_ERROR_NOT_SUPPORTED";
    case CUDA_ERROR_UNKNOWN: return "CUDA_ERROR_UNKNOWN";
    default:
        // This is unfortunate as usually get_cuda_error is called in the middle of
        // an error print, but dropping the number on the floor is worse.
        error(NULL) << "Unknown cuda error " << err << "\n";
        return "<Unknown error>";
    }
}

WEAK halide_device_interface_impl_t cuda_device_interface_impl = {
    halide_use_jit_module,
    halide_release_jit_module,
    halide_cuda_device_malloc,
    halide_cuda_device_free,
    halide_cuda_device_sync,
    halide_cuda_device_release,
    halide_cuda_copy_to_host,
    halide_cuda_copy_to_device,
    halide_cuda_device_and_host_malloc,
    halide_cuda_device_and_host_free,
    halide_cuda_buffer_copy,
    halide_cuda_device_crop,
    halide_cuda_device_slice,
    halide_cuda_device_release_crop,
    halide_cuda_wrap_device_ptr,
    halide_cuda_detach_device_ptr,
};

WEAK halide_device_interface_t cuda_device_interface = {
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
    halide_cuda_compute_capability,
    &cuda_device_interface_impl
};

}}}} // namespace Halide::Runtime::Internal::Cuda
