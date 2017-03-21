#include "HalideRuntimeCuda.h"
#include "device_buffer_utils.h"
#include "device_interface.h"
#include "printer.h"
#include "mini_cuda.h"

#define INLINE inline __attribute__((always_inline))

namespace Halide { namespace Runtime { namespace Internal { namespace Cuda {

// Define the function pointers for the CUDA API.
#define CUDA_FN(ret, fn, args) WEAK ret (CUDAAPI *fn)args;
#define CUDA_FN_3020(ret, fn, fn_3020, args) WEAK ret (CUDAAPI *fn)args;
#define CUDA_FN_4000(ret, fn, fn_4000, args) WEAK ret (CUDAAPI *fn)args;
#include "cuda_functions.h"

// The default implementation of halide_cuda_get_symbol attempts to load
// the CUDA shared library/DLL, and then get the symbol from it.
WEAK void *lib_cuda = NULL;

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
INLINE T get_cuda_symbol(void *user_context, const char *name) {
    T s = (T)halide_cuda_get_symbol(user_context, name);
    if (!s) {
        error(user_context) << "CUDA API not found: " << name << "\n";
    }
    return s;
}

// Load a CUDA shared object/dll and get the CUDA API function pointers from it.
WEAK void load_libcuda(void *user_context) {
    debug(user_context) << "    load_libcuda (user_context: " << user_context << ")\n";
    halide_assert(user_context, cuInit == NULL);

    #define CUDA_FN(ret, fn, args) fn = get_cuda_symbol<ret (CUDAAPI *)args>(user_context, #fn);
    #define CUDA_FN_3020(ret, fn, fn_3020, args) fn = get_cuda_symbol<ret (CUDAAPI *)args>(user_context, #fn_3020);
    #define CUDA_FN_4000(ret, fn, fn_4000, args) fn = get_cuda_symbol<ret (CUDAAPI *)args>(user_context, #fn_4000);
    #include "cuda_functions.h"
}

extern WEAK halide_device_interface_t cuda_device_interface;

WEAK const char *get_error_name(CUresult error);
WEAK CUresult create_cuda_context(void *user_context, CUcontext *ctx);

// A cuda context defined in this module with weak linkage
CUcontext WEAK context = 0;
volatile int WEAK thread_lock = 0;

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

    halide_assert(user_context, &thread_lock != NULL);
    while (__sync_lock_test_and_set(&thread_lock, 1)) { }

    // If the context has not been initialized, initialize it now.
    halide_assert(user_context, &context != NULL);
    if (context == NULL && create) {
        CUresult error = create_cuda_context(user_context, &context);
        if (error != CUDA_SUCCESS) {
            __sync_lock_release(&thread_lock);
            return error;
        }
    }

    *ctx = context;
    return 0;
}

WEAK int halide_cuda_release_context(void *user_context) {
    __sync_lock_release(&thread_lock);
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
        if (cuInit == NULL) {
            load_libcuda(user_context);
        }

#ifdef DEBUG_RUNTIME
        halide_start_clock(user_context);
#endif
        error = halide_cuda_acquire_context(user_context, &context);
        halide_assert(user_context, context != NULL);
        if (error != 0) {
            return;
        }

        error = cuCtxPushCurrent(context);
    }

    INLINE ~Context() {
        CUcontext old;
        cuCtxPopCurrent(&old);

        halide_cuda_release_context(user_context);
    }
};

// Structure to hold the state of a module attached to the context.
// Also used as a linked-list to keep track of all the different
// modules that are attached to a context in order to release them all
// when then context is released.
struct module_state {
    CUmodule module;
    module_state *next;
};
WEAK module_state *state_list = NULL;

WEAK CUresult create_cuda_context(void *user_context, CUcontext *ctx) {
    // Initialize CUDA
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
    err = cuCtxPopCurrent(&context);
    if (err != CUDA_SUCCESS) {
      error(user_context) << "CUDA: cuCtxPopCurrent failed: "
                          << get_error_name(err);
      return err;
    }

    return CUDA_SUCCESS;
}


WEAK bool validate_device_pointer(void *user_context, halide_buffer_t* buf, size_t size=0) {
// The technique using cuPointerGetAttribute and CU_POINTER_ATTRIBUTE_CONTEXT
// requires unified virtual addressing is enabled and that is not the case
// for 32-bit processes on Mac OS X. So for now, as a total hack, just return true
// in 32-bit. This could of course be wrong the other way for cards that only
// support 32-bit addressing in 64-bit processes, but I expect those cards do not
// support unified addressing at all.
// TODO: figure out a way to validate pointers in all cases if strictly necessary.
#ifdef BITS_32
    return true;
#else
    if (buf->device == 0)
        return true;

    CUdeviceptr dev_ptr = (CUdeviceptr)buf->device;

    CUcontext ctx;
    CUresult result = cuPointerGetAttribute(&ctx, CU_POINTER_ATTRIBUTE_CONTEXT, dev_ptr);
    if (result) {
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

    // Create the state object if necessary. This only happens once, regardless
    // of how many times halide_initialize_kernels/halide_release is called.
    // halide_release traverses this list and releases the module objects, but
    // it does not modify the list nodes created/inserted here.
    module_state **state = (module_state**)state_ptr;
    if (!(*state)) {
        *state = (module_state*)malloc(sizeof(module_state));
        (*state)->module = NULL;
        (*state)->next = state_list;
        state_list = *state;
    }

    // Create the module itself if necessary.
    if (!(*state)->module) {
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
        CUresult err = cuModuleLoadDataEx(&(*state)->module, ptx_src, 1, options, optionValues);

        if (err != CUDA_SUCCESS) {
            debug(user_context) << get_error_name(err) << "\n";
            error(user_context) << "CUDA: cuModuleLoadData failed: "
                                << get_error_name(err);
            return err;
        } else {
            debug(user_context) << (void *)((*state)->module) << "\n";
        }
    }

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
    buf->device_interface->release_module();
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

        // Unload the modules attached to this context. Note that the list
        // nodes themselves are not freed, only the module objects are
        // released. Subsequent calls to halide_init_kernels might re-create
        // the program object using the same list node to store the module
        // object.
        module_state *state = state_list;
        while (state) {
            if (state->module) {
                debug(user_context) << "    cuModuleUnload " << state->module << "\n";
                err = cuModuleUnload(state->module);
                halide_assert(user_context, err == CUDA_SUCCESS || err == CUDA_ERROR_DEINITIALIZED);
                state->module = 0;
            }
            state = state->next;
        }

        CUcontext old_ctx;
        cuCtxPopCurrent(&old_ctx);

        // Only destroy the context if we own it
        if (ctx == context) {
            debug(user_context) << "    cuCtxDestroy " << context << "\n";
            err = cuProfilerStop();
            err = cuCtxDestroy(context);
            halide_assert(user_context, err == CUDA_SUCCESS || err == CUDA_ERROR_DEINITIALIZED);
            context = NULL;
        }
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
    buf->device_interface->use_module();

    #ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
    #endif

    return 0;
}

namespace {
WEAK int do_multidimensional_copy(void *user_context, const device_copy &c, uint64_t dst, uint64_t src, int d, bool d_to_h) {
    if (d > MAX_COPY_DIMS) {
        error(user_context) << "Buffer has too many dimensions to copy to/from GPU\n";
        return -1;
    } else if (d == 0) {
        CUresult err = CUDA_SUCCESS;
        const char *copy_name = d_to_h ? "cuMemcpyDtoH" : "cuMemcpyHtoD";
        debug(user_context) << "    " << copy_name << " "
                            << (void *)src << " -> " << (void *)dst << ", " << c.chunk_size << " bytes\n";
        if (d_to_h) {
            err = cuMemcpyDtoH((void *)dst, (CUdeviceptr)src, c.chunk_size);
        } else {
            err = cuMemcpyHtoD((CUdeviceptr)dst, (void *)src, c.chunk_size);
        }
        if (err != CUDA_SUCCESS) {
            error(user_context) << "CUDA: " << copy_name << " failed: " << get_error_name(err);
            return (int)err;
        }
    } else {
        ssize_t off = 0;
        for (int i = 0; i < (int)c.extent[d-1]; i++) {
            int err = do_multidimensional_copy(user_context, c, dst + off, src + off, d-1, d_to_h);
            off += c.stride_bytes[d-1];
            if (err) {
                return err;
            }
        }
    }
    return 0;
}
}

WEAK int halide_cuda_copy_to_device(void *user_context, halide_buffer_t* buf) {
    debug(user_context)
        <<  "CUDA: halide_cuda_copy_to_device (user_context: " << user_context
        << ", buf: " << buf << ")\n";

    Context ctx(user_context);
    if (ctx.error != CUDA_SUCCESS) {
        return ctx.error;
    }

    #ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
    #endif

    halide_assert(user_context, buf->host && buf->device);
    halide_assert(user_context, validate_device_pointer(user_context, buf));

    device_copy c = make_host_to_device_copy(buf);

    int err = do_multidimensional_copy(user_context, c, c.dst, c.src, buf->dimensions, false);
    if (err) {
        return err;
    }

    #ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
    #endif

    return 0;
}

WEAK int halide_cuda_copy_to_host(void *user_context, halide_buffer_t* buf) {
    debug(user_context)
        << "CUDA: halide_cuda_copy_to_host (user_context: " << user_context
        << ", buf: " << buf << ")\n";

    Context ctx(user_context);
    if (ctx.error != CUDA_SUCCESS) {
        return ctx.error;
    }

    #ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
    #endif

    halide_assert(user_context, buf->device && buf->device);
    halide_assert(user_context, validate_device_pointer(user_context, buf));

    device_copy c = make_device_to_host_copy(buf);

    int err = do_multidimensional_copy(user_context, c, c.dst, c.src, buf->dimensions, true);
    if (err) {
        return err;
    }

    #ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
    #endif

    return 0;
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

    CUresult err = cuCtxSynchronize();
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
    CUmodule mod = ((module_state*)state_ptr)->module;
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
            dev_handles[i] = (*(uint64_t *)args[i]);
            translated_args[i] = &(dev_handles[i]);
            debug(user_context) << "    halide_cuda_run translated arg" << (int)i
                                << " [" << (*((void **)translated_args[i])) << " ...]\n";
        } else {
            translated_args[i] = args[i];
        }
    }

    err = cuLaunchKernel(f,
                         blocksX,  blocksY,  blocksZ,
                         threadsX, threadsY, threadsZ,
                         shared_mem_bytes,
                         NULL, // stream
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

WEAK int halide_cuda_wrap_device_ptr(void *user_context, struct halide_buffer_t *buf, uintptr_t device_ptr) {
    halide_assert(user_context, buf->device == 0);
    if (buf->device != 0) {
        return -2;
    }
    buf->device = device_ptr;
    buf->device_interface = &cuda_device_interface;
    buf->device_interface->use_module();
#if DEBUG_RUNTIME
    if (!validate_device_pointer(user_context, buf)) {
        buf->device_interface->release_module();
        buf->device = 0;
        buf->device_interface = NULL;
        return -3;
    }
#endif
    return 0;
}

WEAK uintptr_t halide_cuda_detach_device_ptr(void *user_context, struct halide_buffer_t *buf) {
    if (buf->device == NULL) {
        return 0;
    }
    halide_assert(user_context, buf->device_interface == &cuda_device_interface);
    uint64_t dev_ptr = buf->device;
    buf->device_interface->release_module();
    buf->device = 0;
    buf->device_interface = NULL;
    return (uintptr_t)dev_ptr;
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

namespace {
__attribute__((destructor))
WEAK void halide_cuda_cleanup() {
    halide_cuda_device_release(NULL);
}
}

} // extern "C" linkage

namespace Halide { namespace Runtime { namespace Internal { namespace Cuda {

WEAK const char *get_error_name(CUresult error) {
    switch(error) {
    case CUDA_SUCCESS: return "CUDA_SUCCESS";
    case CUDA_ERROR_INVALID_VALUE: return "CUDA_ERROR_INVALID_VALUE";
    case CUDA_ERROR_OUT_OF_MEMORY: return "CUDA_ERROR_OUT_OF_MEMORY";
    case CUDA_ERROR_NOT_INITIALIZED: return "CUDA_ERROR_NOT_INITIALIZED";
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
    case CUDA_ERROR_INVALID_SOURCE: return "CUDA_ERROR_INVALID_SOURCE";
    case CUDA_ERROR_FILE_NOT_FOUND: return "CUDA_ERROR_FILE_NOT_FOUND";
    case CUDA_ERROR_INVALID_HANDLE: return "CUDA_ERROR_INVALID_HANDLE";
    case CUDA_ERROR_NOT_FOUND: return "CUDA_ERROR_NOT_FOUND";
    case CUDA_ERROR_NOT_READY: return "CUDA_ERROR_NOT_READY";
    case CUDA_ERROR_LAUNCH_FAILED: return "CUDA_ERROR_LAUNCH_FAILED";
    case CUDA_ERROR_LAUNCH_OUT_OF_RESOURCES: return "CUDA_ERROR_LAUNCH_OUT_OF_RESOURCES";
    case CUDA_ERROR_LAUNCH_TIMEOUT: return "CUDA_ERROR_LAUNCH_TIMEOUT";
    case CUDA_ERROR_LAUNCH_INCOMPATIBLE_TEXTURING: return "CUDA_ERROR_LAUNCH_INCOMPATIBLE_TEXTURING";
    case CUDA_ERROR_UNKNOWN: return "CUDA_ERROR_UNKNOWN";
    // A trap instruction produces the below error, which is how we codegen asserts on GPU
    case CUDA_ERROR_ILLEGAL_INSTRUCTION:
        return "Illegal instruction or Halide assertion failure inside kernel";
    default: return "<Unknown error>";
    }
}

WEAK halide_device_interface_t cuda_device_interface = {
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
};

}}}} // namespace Halide::Runtime::Internal::Cuda
