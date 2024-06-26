#include "HalideRuntimeCuda.h"
#include "device_buffer_utils.h"
#include "device_interface.h"
#include "gpu_context_common.h"
#include "mini_cuda.h"
#include "printer.h"
#include "scoped_mutex_lock.h"
#include "scoped_spin_lock.h"

namespace Halide {
namespace Runtime {
namespace Internal {
namespace Cuda {

// Define the function pointers for the CUDA API.

// clang-format off
#define CUDA_FN(ret, fn, args)                  WEAK ret(CUDAAPI *fn) args;  // NOLINT(bugprone-macro-parentheses)
#define CUDA_FN_OPTIONAL(ret, fn, args)         WEAK ret(CUDAAPI *fn) args;  // NOLINT(bugprone-macro-parentheses)
#define CUDA_FN_3020(ret, fn, fn_3020, args)    WEAK ret(CUDAAPI *fn) args;  // NOLINT(bugprone-macro-parentheses)
#define CUDA_FN_4000(ret, fn, fn_4000, args)    WEAK ret(CUDAAPI *fn) args;  // NOLINT(bugprone-macro-parentheses)
#include "cuda_functions.h"
#undef CUDA_FN
#undef CUDA_FN_OPTIONAL
#undef CUDA_FN_3020
#undef CUDA_FN_4000
// clang-format on

// The default implementation of halide_cuda_get_symbol attempts to load
// the CUDA shared library/DLL, and then get the symbol from it.
WEAK void *lib_cuda = nullptr;
volatile ScopedSpinLock::AtomicFlag WEAK lib_cuda_lock = 0;

extern "C" WEAK void *halide_cuda_get_symbol(void *user_context, const char *name) {
    // Only try to load the library if we can't already get the symbol
    // from the library. Even if the library is nullptr, the symbols may
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
    for (auto &lib_name : lib_names) {
        lib_cuda = halide_load_library(lib_name);
        if (lib_cuda) {
            debug(user_context) << "    Loaded CUDA runtime library: " << lib_name << "\n";
            break;
        }
    }

    return halide_get_library_symbol(lib_cuda, name);
}

template<typename T>
ALWAYS_INLINE halide_error_code_t get_cuda_symbol(void *user_context, const char *name, bool optional, T &fn) {
    fn = (T)halide_cuda_get_symbol(user_context, name);
    if (!optional && !fn) {
        error(user_context) << "CUDA API not found: " << name;
        return halide_error_code_symbol_not_found;
    }
    return halide_error_code_success;
}

// Load a CUDA shared object/dll and get the CUDA API function pointers from it.
WEAK int load_libcuda(void *user_context) {
    debug(user_context) << "    load_libcuda (user_context: " << user_context << ")\n";
    halide_abort_if_false(user_context, cuInit == nullptr);
    halide_error_code_t result;

    // clang-format off
#define CUDA_FN(ret, fn, args)               result = get_cuda_symbol<ret(CUDAAPI *) args>(user_context, #fn, false, fn); if (result) return result;        // NOLINT(bugprone-macro-parentheses)
#define CUDA_FN_OPTIONAL(ret, fn, args)      result = get_cuda_symbol<ret(CUDAAPI *) args>(user_context, #fn, true, fn); if (result) return result; // NOLINT(bugprone-macro-parentheses)
#define CUDA_FN_3020(ret, fn, fn_3020, args) result = get_cuda_symbol<ret(CUDAAPI *) args>(user_context, #fn_3020, false, fn); if (result) return result;  // NOLINT(bugprone-macro-parentheses)
#define CUDA_FN_4000(ret, fn, fn_4000, args) result = get_cuda_symbol<ret(CUDAAPI *) args>(user_context, #fn_4000, false, fn); if (result) return result;  // NOLINT(bugprone-macro-parentheses)
#include "cuda_functions.h"
#undef CUDA_FN
#undef CUDA_FN_OPTIONAL
#undef CUDA_FN_3020
#undef CUDA_FN_4000
    // clang-format on
    return halide_error_code_success;
}

// Call load_libcuda() if CUDA library has not been loaded.
// This function is thread safe.
// Note that initialization might fail. The caller can detect such failure by checking whether cuInit is nullptr.
WEAK int ensure_libcuda_init(void *user_context) {
    ScopedSpinLock spinlock(&lib_cuda_lock);
    if (!cuInit) {
        return load_libcuda(user_context);
    }
    return halide_error_code_success;
}

extern WEAK halide_device_interface_t cuda_device_interface;

WEAK const char *get_cuda_error_name(CUresult error);
WEAK int create_cuda_context(void *user_context, CUcontext *ctx);

template<typename... Args>
int error_cuda(void *user_context, CUresult cuda_error, const Args &...args) {
    if (cuda_error == CUDA_SUCCESS) {
        return halide_error_code_success;
    }
    error(user_context).append("CUDA error: ", get_cuda_error_name(cuda_error), " ", args...);
    return halide_error_code_gpu_device_error;
}

// A cuda context defined in this module with weak linkage
CUcontext WEAK context = nullptr;
// This lock protexts the above context variable.
WEAK halide_mutex context_lock;

// A free list, used when allocations are being cached.
WEAK struct FreeListItem {
    CUdeviceptr ptr;
    CUcontext ctx;
    CUstream stream;
    size_t size;
    FreeListItem *next;
} *free_list = nullptr;
WEAK halide_mutex free_list_lock;

}  // namespace Cuda
}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

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
WEAK int halide_default_cuda_acquire_context(void *user_context, CUcontext *ctx, bool create = true) {
    // TODO: Should we use a more "assertive" assert? these asserts do
    // not block execution on failure.
    halide_abort_if_false(user_context, ctx != nullptr);

    // If the context has not been initialized, initialize it now.
    halide_abort_if_false(user_context, &context != nullptr);

    // Note that this null-check of the context is *not* locked with
    // respect to device_release, so we may get a non-null context
    // that's in the process of being destroyed. Things will go badly
    // in general if you call device_release while other Halide code
    // is running though.
    CUcontext local_val = context;
    if (local_val == nullptr) {
        if (!create) {
            *ctx = nullptr;
            return halide_error_code_success;
        }

        {
            ScopedMutexLock spinlock(&context_lock);
            local_val = context;
            if (local_val == nullptr) {
                if (auto result = create_cuda_context(user_context, &local_val);
                    result != halide_error_code_success) {
                    return result;
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
    return halide_error_code_success;
}

WEAK int halide_default_cuda_release_context(void *user_context) {
    return halide_error_code_success;
}

// Return the stream to use for executing kernels and synchronization. Only called
// for versions of cuda which support streams. Default is to use the main stream
// for the context (nullptr stream). The context is passed in for convenience, but
// any sort of scoping must be handled by that of the
// halide_cuda_acquire_context/halide_cuda_release_context pair, not this call.
WEAK int halide_default_cuda_get_stream(void *user_context, CUcontext ctx, CUstream *stream) {
    // There are two default streams we could use. stream 0 is fully
    // synchronous. stream 2 gives a separate non-blocking stream per
    // thread.
    *stream = nullptr;
    return halide_error_code_success;
}

}  // extern "C"

namespace Halide {
namespace Runtime {
namespace Internal {
namespace CUDA {

WEAK halide_cuda_acquire_context_t acquire_context = (halide_cuda_acquire_context_t)halide_default_cuda_acquire_context;
WEAK halide_cuda_release_context_t release_context = (halide_cuda_release_context_t)halide_default_cuda_release_context;
WEAK halide_cuda_get_stream_t get_stream = (halide_cuda_get_stream_t)halide_default_cuda_get_stream;

}  // namespace CUDA
}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

extern "C" {

WEAK int halide_cuda_acquire_context(void *user_context, CUcontext *ctx, bool create = true) {
    return CUDA::acquire_context(user_context, (void **)ctx, create);
}

WEAK halide_cuda_acquire_context_t halide_set_cuda_acquire_context(halide_cuda_acquire_context_t handler) {
    halide_cuda_acquire_context_t result = CUDA::acquire_context;
    CUDA::acquire_context = handler;
    return result;
}

WEAK int halide_cuda_release_context(void *user_context) {
    return CUDA::release_context(user_context);
}

WEAK halide_cuda_release_context_t halide_set_cuda_release_context(halide_cuda_release_context_t handler) {
    halide_cuda_release_context_t result = CUDA::release_context;
    CUDA::release_context = handler;
    return result;
}

WEAK int halide_cuda_get_stream(void *user_context, CUcontext ctx, CUstream *stream) {
    return CUDA::get_stream(user_context, (void *)ctx, (void **)stream);
}

WEAK halide_cuda_get_stream_t halide_set_cuda_get_stream(halide_cuda_get_stream_t handler) {
    halide_cuda_get_stream_t result = CUDA::get_stream;
    CUDA::get_stream = handler;
    return result;
}
}

namespace Halide {
namespace Runtime {
namespace Internal {
namespace Cuda {

// Helper object to acquire and release the cuda context.
class Context {
    void *const user_context;
    int status = halide_error_code_success;  // must always be a valid halide_error_code_t value

public:
    CUcontext context = nullptr;

    // Constructor sets 'status' if any error occurs.
    ALWAYS_INLINE explicit Context(void *user_context)
        : user_context(user_context) {
#ifdef DEBUG_RUNTIME
        halide_start_clock(user_context);
#endif
        status = halide_cuda_acquire_context(user_context, &context);
        if (status) {
            return;
        }

        // The default acquire_context loads libcuda as a
        // side-effect. However, if acquire_context has been
        // overridden, we may still need to load libcuda
        status = ensure_libcuda_init(user_context);
        if (status) {
            return;
        }

        halide_abort_if_false(user_context, context != nullptr);
        halide_abort_if_false(user_context, cuInit != nullptr);

        status = error_cuda(user_context, cuCtxPushCurrent(context));
    }

    ALWAYS_INLINE ~Context() {
        if (status == halide_error_code_success) {
            CUcontext old;
            cuCtxPopCurrent(&old);
        }

        (void)halide_cuda_release_context(user_context);  // ignore error
    }

    ALWAYS_INLINE int error() const {
        return status;
    }

    Context(const Context &) = delete;
    Context &operator=(const Context &) = delete;
    Context(Context &&) = delete;
    Context &operator=(Context &&) = delete;
};

WEAK Halide::Internal::GPUCompilationCache<CUcontext, CUmodule> compilation_cache;

WEAK int create_cuda_context(void *user_context, CUcontext *ctx) {
    // Initialize CUDA
    auto result = ensure_libcuda_init(user_context);
    if (result) {
        return result;
    }
    if (!cuInit) {
        return error_cuda(user_context, CUDA_ERROR_FILE_NOT_FOUND, "Could not find cuda system libraries");
    }

    CUresult err = cuInit(0);
    if (err != CUDA_SUCCESS) {
        return error_cuda(user_context, err, "cuInit failed");
    }

    // Make sure we have a device
    int deviceCount = 0;
    err = cuDeviceGetCount(&deviceCount);
    if (err != CUDA_SUCCESS) {
        return error_cuda(user_context, err, "cuGetDeviceCount failed");
    }

    if (deviceCount <= 0) {
        return error_cuda(user_context, CUDA_ERROR_NO_DEVICE, "No devices available");
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
    err = cuDeviceGet(&dev, device);
    if (err != CUDA_SUCCESS) {
        return error_cuda(user_context, err, "Failed to get device");
    }

    debug(user_context) << "    Got device " << dev << "\n";

// Dump device attributes
#ifdef DEBUG_RUNTIME
    {
        char name[256];
        name[0] = 0;
        err = cuDeviceGetName(name, 256, dev);
        debug(user_context) << "      " << name << "\n";

        if (err != CUDA_SUCCESS) {
            return error_cuda(user_context, err, "cuDeviceGetName failed");
        }

        size_t memory = 0;
        err = cuDeviceTotalMem(&memory, dev);
        debug(user_context) << "      total memory: " << (int)(memory >> 20) << " MB\n";

        if (err != CUDA_SUCCESS) {
            return error_cuda(user_context, err, "cuDeviceTotalMem failed");
        }

        // Declare variables for other state we want to query.
        int max_threads_per_block = 0, warp_size = 0, num_cores = 0;
        int max_block_size[] = {0, 0, 0};
        int max_grid_size[] = {0, 0, 0};
        int max_shared_mem = 0, max_constant_mem = 0;
        int cc_major = 0, cc_minor = 0;

        struct {
            int *dst;
            CUdevice_attribute attr;
        } attrs[] = {
            {&max_threads_per_block, CU_DEVICE_ATTRIBUTE_MAX_THREADS_PER_BLOCK},
            {&warp_size, CU_DEVICE_ATTRIBUTE_WARP_SIZE},
            {&num_cores, CU_DEVICE_ATTRIBUTE_MULTIPROCESSOR_COUNT},
            {&max_block_size[0], CU_DEVICE_ATTRIBUTE_MAX_BLOCK_DIM_X},
            {&max_block_size[1], CU_DEVICE_ATTRIBUTE_MAX_BLOCK_DIM_Y},
            {&max_block_size[2], CU_DEVICE_ATTRIBUTE_MAX_BLOCK_DIM_Z},
            {&max_grid_size[0], CU_DEVICE_ATTRIBUTE_MAX_GRID_DIM_X},
            {&max_grid_size[1], CU_DEVICE_ATTRIBUTE_MAX_GRID_DIM_Y},
            {&max_grid_size[2], CU_DEVICE_ATTRIBUTE_MAX_GRID_DIM_Z},
            {&max_shared_mem, CU_DEVICE_ATTRIBUTE_SHARED_MEMORY_PER_BLOCK},
            {&max_constant_mem, CU_DEVICE_ATTRIBUTE_TOTAL_CONSTANT_MEMORY},
            {&cc_major, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR},
            {&cc_minor, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR},
            {nullptr, CU_DEVICE_ATTRIBUTE_MAX}};

        // Do all the queries.
        for (int i = 0; attrs[i].dst; i++) {
            err = cuDeviceGetAttribute(attrs[i].dst, attrs[i].attr, dev);
            if (err != CUDA_SUCCESS) {
                return error_cuda(user_context, err, "cuDeviceGetAttribute failed for attribute ", (int)attrs[i].attr);
            }
        }

        // threads per core is a function of the compute capability
        int threads_per_core;
        switch (cc_major) {
        case 1:
            threads_per_core = 8;
            break;
        case 2:
            threads_per_core = (cc_minor == 0 ? 32 : 48);
            break;
        case 3:
            threads_per_core = 192;
            break;
        case 5:
            threads_per_core = 128;
            break;
        case 6:
            threads_per_core = (cc_minor == 0 ? 64 : 128);
            break;
        case 7:
            threads_per_core = 64;
            break;
        case 8:
            threads_per_core = 128;
            break;
        default:
            threads_per_core = 0;
            break;
        }

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
            << "      cuda cores: " << num_cores << " x " << threads_per_core
            << " = " << num_cores * threads_per_core << "\n";
    }
#endif

    // Create context
    debug(user_context) << "    cuCtxCreate " << dev << " -> ";
    err = cuCtxCreate(ctx, 0, dev);
    if (err != CUDA_SUCCESS) {
        return error_cuda(user_context, err, "cuCtxCreate failed");
    }
    unsigned int version = 0;
    err = cuCtxGetApiVersion(*ctx, &version);
    if (err != CUDA_SUCCESS) {
        return error_cuda(user_context, err, "cuCtxGetApiVersion failed");
    }
    debug(user_context) << *ctx << "(" << version << ")\n";

    // Creation automatically pushes the context, but we'll pop to allow the caller
    // to decide when to push.
    CUcontext dummy;
    err = cuCtxPopCurrent(&dummy);
    if (err != CUDA_SUCCESS) {
        return error_cuda(user_context, err, "cuCtxPopCurrent failed");
    }

    return halide_error_code_success;
}

// This feature may be useful during CUDA backend or runtime
// development. It does not seem to find many errors in general Halide
// use and causes false positives in at least one environment, where
// it prevents using debug mode with cuda.
#define ENABLE_POINTER_VALIDATION 0

WEAK int validate_device_pointer(void *user_context, halide_buffer_t *buf, size_t size = 0) {
#if !ENABLE_POINTER_VALIDATION
    return halide_error_code_success;
#else
    if (buf->device != 0) {
        CUdeviceptr dev_ptr = (CUdeviceptr)buf->device;

        CUcontext ctx;
        CUresult err = cuPointerGetAttribute(&ctx, CU_POINTER_ATTRIBUTE_CONTEXT, dev_ptr);
        if (err != CUDA_SUCCESS) {
            return error_cuda(user_context, err, "Bad device pointer ", (void *)dev_ptr);
        }
    }
    return halide_error_code_success;
#endif
}

WEAK CUmodule compile_kernel(void *user_context, const char *ptx_src, int size) {
    debug(user_context) << "CUDA: compile_kernel cuModuleLoadData " << (void *)ptx_src << ", " << size << " -> ";

    CUjit_option options[] = {CU_JIT_MAX_REGISTERS};
    unsigned int max_regs_per_thread = 64;

    // A hack to enable control over max register count for
    // testing. This should be surfaced in the schedule somehow
    // instead.
    char *regs = getenv("HL_CUDA_JIT_MAX_REGISTERS");
    if (regs) {
        max_regs_per_thread = atoi(regs);
    }
    void *optionValues[] = {(void *)(uintptr_t)max_regs_per_thread};
    CUmodule loaded_module;
    CUresult err = cuModuleLoadDataEx(&loaded_module, ptx_src, 1, options, optionValues);

    if (err != CUDA_SUCCESS) {
        error(user_context) << "CUDA: cuModuleLoadData failed: "
                            << get_cuda_error_name(err);
        return nullptr;
    } else {
        debug(user_context) << (void *)(loaded_module) << "\n";
    }
    return loaded_module;
}

}  // namespace Cuda
}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

extern "C" {
WEAK int halide_cuda_initialize_kernels(void *user_context, void **state_ptr, const char *ptx_src, int size) {
    debug(user_context) << "CUDA: halide_cuda_initialize_kernels (user_context: " << user_context
                        << ", state_ptr: " << state_ptr
                        << ", ptx_src: " << (void *)ptx_src
                        << ", size: " << size << "\n";

    Context ctx(user_context);
    if (ctx.error()) {
        return ctx.error();
    }

#ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
#endif

    CUmodule loaded_module;
    if (!compilation_cache.kernel_state_setup(user_context, state_ptr, ctx.context, loaded_module,
                                              compile_kernel, user_context, ptx_src, size)) {
        return halide_error_code_generic_error;
    }
    halide_abort_if_false(user_context, loaded_module != nullptr);

#ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
#endif

    return halide_error_code_success;
}

WEAK void halide_cuda_finalize_kernels(void *user_context, void *state_ptr) {
    Context ctx(user_context);
    if (ctx.error() == halide_error_code_success) {
        compilation_cache.release_hold(user_context, ctx.context, state_ptr);
    }
}

WEAK int halide_cuda_release_unused_device_allocations(void *user_context) {
    FreeListItem *to_free;
    {
        ScopedMutexLock lock(&free_list_lock);
        to_free = free_list;
        free_list = nullptr;
    }
    while (to_free) {
        debug(user_context) << "    cuMemFree " << (void *)(to_free->ptr) << "\n";
        cuMemFree(to_free->ptr);
        FreeListItem *next = to_free->next;
        free(to_free);
        to_free = next;
    }
    return halide_error_code_success;
}

namespace Halide {
namespace Runtime {
namespace Internal {

WEAK halide_device_allocation_pool cuda_allocation_pool;

WEAK __attribute__((constructor)) void register_cuda_allocation_pool() {
    cuda_allocation_pool.release_unused = &halide_cuda_release_unused_device_allocations;
    halide_register_device_allocation_pool(&cuda_allocation_pool);
}

ALWAYS_INLINE uint64_t quantize_allocation_size(uint64_t sz) {
    int z = __builtin_clzll(sz);
    if (z < 60) {
        sz--;
        sz = sz >> (60 - z);
        sz++;
        sz = sz << (60 - z);
    }
    return sz;
}

}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

WEAK int halide_cuda_device_free(void *user_context, halide_buffer_t *buf) {
    // halide_device_free, at present, can be exposed to clients and they
    // should be allowed to call halide_device_free on any halide_buffer_t
    // including ones that have never been used with a GPU.
    if (buf->device == 0) {
        return halide_error_code_success;
    }

    CUdeviceptr dev_ptr = (CUdeviceptr)buf->device;

    debug(user_context)
        << "CUDA: halide_cuda_device_free (user_context: " << user_context
        << ", buf: " << buf << ")\n";

    Context ctx(user_context);
    if (ctx.error()) {
        return ctx.error();
    }

#ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
#endif

    auto result = validate_device_pointer(user_context, buf);
    if (result) {
        return result;
    }

    CUresult err = CUDA_SUCCESS;
    if (halide_can_reuse_device_allocations(user_context)) {
        debug(user_context) << "    caching allocation for later use: " << (void *)(dev_ptr) << "\n";

        FreeListItem *item = (FreeListItem *)malloc(sizeof(FreeListItem));
        item->ctx = ctx.context;
        item->size = quantize_allocation_size(buf->size_in_bytes());
        item->ptr = dev_ptr;

        if (cuStreamSynchronize) {
            // We don't want to use a buffer freed one stream on
            // another, as there are no synchronization guarantees and
            // everything is async.
            result = halide_cuda_get_stream(user_context, ctx.context, &item->stream);
            if (result) {
                return result;
            }
        } else {
            item->stream = nullptr;
        }

        {
            ScopedMutexLock lock(&free_list_lock);
            item->next = free_list;
            free_list = item;
        }
    } else {
        debug(user_context) << "    cuMemFree " << (void *)(dev_ptr) << "\n";
        err = cuMemFree(dev_ptr);
        // If cuMemFree fails, it isn't likely to succeed later, so just drop
        // the reference.
    }
    buf->device_interface->impl->release_module();
    buf->device_interface = nullptr;
    buf->device = 0;
    if (err != CUDA_SUCCESS) {
        // We may be called as a destructor, so don't raise an error here.
        return error_cuda(user_context, err);
    }

#ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
#endif

    return halide_error_code_success;
}

WEAK int halide_cuda_device_release(void *user_context) {
    debug(user_context)
        << "CUDA: halide_cuda_device_release (user_context: " << user_context << ")\n";

    // If we haven't even loaded libcuda, don't load it just to quit.
    if (!cuInit) {
        return halide_error_code_success;
    }

    CUcontext ctx;
    auto result = halide_cuda_acquire_context(user_context, &ctx, false);
    if (result) {
        return result;
    }

    if (ctx) {
        // It's possible that this is being called from the destructor of
        // a static variable, in which case the driver may already be
        // shutting down.
        CUresult err = cuCtxPushCurrent(ctx);
        if (err != CUDA_SUCCESS) {
            err = cuCtxSynchronize();
        }
        if (err != CUDA_SUCCESS && err != CUDA_ERROR_DEINITIALIZED) {
            return error_cuda(user_context, err);
        }

        // Dump the contents of the free list, ignoring errors.
        (void)halide_cuda_release_unused_device_allocations(user_context);

        compilation_cache.delete_context(user_context, ctx, cuModuleUnload);

        CUcontext old_ctx;
        cuCtxPopCurrent(&old_ctx);

        // Only destroy the context if we own it

        {
            ScopedMutexLock spinlock(&context_lock);

            if (ctx == context) {
                debug(user_context) << "    cuCtxDestroy " << context << "\n";
                err = cuProfilerStop();
                err = cuCtxDestroy(context);
                if (err != CUDA_SUCCESS && err != CUDA_ERROR_DEINITIALIZED) {
                    return error_cuda(user_context, err);
                }
                context = nullptr;
            }
        }  // spinlock
    }

    return halide_cuda_release_context(user_context);
}

WEAK int halide_cuda_device_malloc(void *user_context, halide_buffer_t *buf) {
    debug(user_context)
        << "CUDA: halide_cuda_device_malloc (user_context: " << user_context
        << ", buf: " << buf << ")\n";

    Context ctx(user_context);
    if (ctx.error()) {
        return ctx.error();
    }

    size_t size = buf->size_in_bytes();
    if (halide_can_reuse_device_allocations(user_context)) {
        size = quantize_allocation_size(size);
    }
    halide_abort_if_false(user_context, size != 0);
    if (buf->device) {
        // This buffer already has a device allocation
        return validate_device_pointer(user_context, buf, size);
    }

    // Check all strides positive.
    for (int i = 0; i < buf->dimensions; i++) {
        halide_abort_if_false(user_context, buf->dim[i].stride >= 0);
    }

    debug(user_context) << "    allocating " << *buf << "\n";

#ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
#endif

    CUdeviceptr p = 0;
    FreeListItem *to_free = nullptr;
    if (halide_can_reuse_device_allocations(user_context)) {
        CUstream stream = nullptr;
        if (cuStreamSynchronize != nullptr) {
            auto result = halide_cuda_get_stream(user_context, ctx.context, &stream);
            if (result) {
                return result;
            }
        }

        ScopedMutexLock lock(&free_list_lock);
        // Best-fit allocation. There are three tunable constants
        // here. A bucket is claimed if the size requested is at least
        // 7/8 of the size of the bucket. We keep at most 32 unused
        // allocations. We round up each allocation size to its top 4
        // most significant bits (see quantize_allocation_size).
        FreeListItem *best = nullptr, *item = free_list;
        FreeListItem **best_prev = nullptr, **prev_ptr = &free_list;
        int depth = 0;
        while (item) {
            if ((size <= item->size) &&                              // Fits
                (size >= (item->size / 8) * 7) &&                    // Not too much slop
                (ctx.context == item->ctx) &&                        // Same cuda context
                (stream == item->stream) &&                          // Can only safely re-use on the same stream on which it was freed
                ((best == nullptr) || (best->size > item->size))) {  // Better than previous best fit
                best = item;
                best_prev = prev_ptr;
                prev_ptr = &item->next;
                item = item->next;
            } else if (depth > 32) {
                // Allocations after here have not been used for a
                // long time. Just detach the rest of the free list
                // and defer the actual cuMemFree calls until after we
                // release the free_list_lock.
                to_free = item;
                *prev_ptr = nullptr;
                item = nullptr;
                break;
            } else {
                prev_ptr = &item->next;
                item = item->next;
            }
            depth++;
        }

        if (best) {
            p = best->ptr;
            *best_prev = best->next;
            free(best);
        }
    }

    while (to_free) {
        FreeListItem *next = to_free->next;
        cuMemFree(to_free->ptr);
        free(to_free);
        to_free = next;
    }

    if (!p) {
        debug(user_context) << "    cuMemAlloc " << (uint64_t)size << " -> ";

        // Quantize all allocation sizes to the top 4 bits, to make
        // reuse likelier. Wastes on average 4% memory per allocation.

        CUresult err = cuMemAlloc(&p, size);
        if (err == CUDA_ERROR_OUT_OF_MEMORY) {
            auto result = halide_cuda_release_unused_device_allocations(user_context);
            if (result) {
                return result;
            }
            err = cuMemAlloc(&p, size);
        }
        if (err != CUDA_SUCCESS) {
            return error_cuda(user_context, err, "cuMemAlloc failed");
        }
        debug(user_context) << (void *)p << "\n";
    }
    halide_abort_if_false(user_context, p);
    buf->device = p;
    buf->device_interface = &cuda_device_interface;
    buf->device_interface->impl->use_module();

#ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
#endif

    return halide_error_code_success;
}

namespace {
WEAK int cuda_do_multidimensional_copy(void *user_context, const device_copy &c,
                                       uint64_t src, uint64_t dst, int d, bool from_host, bool to_host,
                                       CUstream stream) {
    if (d > MAX_COPY_DIMS) {
        error(user_context) << "Buffer has too many dimensions to copy to/from GPU\n";
        return halide_error_code_bad_dimensions;
    } else if (d == 0) {
        CUresult err = CUDA_SUCCESS;
        const char *copy_name;
        debug(user_context) << "    from " << (from_host ? "host" : "device")
                            << " to " << (to_host ? "host" : "device") << ", "
                            << (void *)src << " -> " << (void *)dst << ", " << c.chunk_size << " bytes\n";
        if (!from_host && to_host) {
            debug(user_context) << "cuMemcpyDtoH(" << (void *)dst << ", " << (void *)src << ", " << c.chunk_size << ")\n";
            copy_name = "cuMemcpyDtoH";
            if (stream) {
                err = cuMemcpyDtoHAsync((void *)dst, (CUdeviceptr)src, c.chunk_size, stream);
            } else {
                err = cuMemcpyDtoH((void *)dst, (CUdeviceptr)src, c.chunk_size);
            }
        } else if (from_host && !to_host) {
            debug(user_context) << "cuMemcpyHtoD(" << (void *)dst << ", " << (void *)src << ", " << c.chunk_size << ")\n";
            copy_name = "cuMemcpyHtoD";
            if (stream) {
                err = cuMemcpyHtoDAsync((CUdeviceptr)dst, (void *)src, c.chunk_size, stream);
            } else {
                err = cuMemcpyHtoD((CUdeviceptr)dst, (void *)src, c.chunk_size);
            }
        } else if (!from_host && !to_host) {
            debug(user_context) << "cuMemcpyDtoD(" << (void *)dst << ", " << (void *)src << ", " << c.chunk_size << ")\n";
            copy_name = "cuMemcpyDtoD";
            if (stream) {
                err = cuMemcpyDtoDAsync((CUdeviceptr)dst, (CUdeviceptr)src, c.chunk_size, stream);
            } else {
                err = cuMemcpyDtoD((CUdeviceptr)dst, (CUdeviceptr)src, c.chunk_size);
            }
        } else if (dst != src) {
            debug(user_context) << "memcpy(" << (void *)dst << ", " << (void *)src << ", " << c.chunk_size << ")\n";
            // Could reach here if a user called directly into the
            // cuda API for a device->host copy on a source buffer
            // with device_dirty = false.
            memcpy((void *)dst, (void *)src, c.chunk_size);
        }
        if (err != CUDA_SUCCESS) {
            return error_cuda(user_context, err, copy_name, " failed");
        }
    } else {
        ssize_t src_off = 0, dst_off = 0;
        for (int i = 0; i < (int)c.extent[d - 1]; i++) {
            auto result = cuda_do_multidimensional_copy(user_context, c, src + src_off, dst + dst_off, d - 1, from_host, to_host, stream);
            dst_off += c.dst_stride_bytes[d - 1];
            src_off += c.src_stride_bytes[d - 1];
            if (result) {
                return result;
            }
        }
    }
    return halide_error_code_success;
}
}  // namespace

WEAK int halide_cuda_buffer_copy(void *user_context, struct halide_buffer_t *src,
                                 const struct halide_device_interface_t *dst_device_interface,
                                 struct halide_buffer_t *dst) {
    // We only handle copies to cuda or to host
    halide_abort_if_false(user_context, dst_device_interface == nullptr ||
                                            dst_device_interface == &cuda_device_interface);

    if ((src->device_dirty() || src->host == nullptr) &&
        src->device_interface != &cuda_device_interface) {
        halide_abort_if_false(user_context, dst_device_interface == &cuda_device_interface);
        // This is handled at the higher level.
        return halide_error_code_incompatible_device_interface;
    }

    bool from_host = (src->device_interface != &cuda_device_interface) ||
                     (src->device == 0) ||
                     (src->host_dirty() && src->host != nullptr);
    bool to_host = !dst_device_interface;

    halide_abort_if_false(user_context, from_host || src->device);
    halide_abort_if_false(user_context, to_host || dst->device);

    device_copy c = make_buffer_copy(src, from_host, dst, to_host);

    {
        Context ctx(user_context);
        if (ctx.error()) {
            return ctx.error();
        }

        debug(user_context)
            << "CUDA: halide_cuda_buffer_copy (user_context: " << user_context
            << ", src: " << src << ", dst: " << dst << ")\n";

#ifdef DEBUG_RUNTIME
        uint64_t t_before = halide_current_time_ns(user_context);
        if (!from_host) {
            auto result = validate_device_pointer(user_context, src);
            if (result) {
                return result;
            }
        }
        if (!to_host) {
            auto result = validate_device_pointer(user_context, dst);
            if (result) {
                return result;
            }
        }
#endif

        CUstream stream = nullptr;
        if (cuStreamSynchronize != nullptr) {
            auto result = halide_cuda_get_stream(user_context, ctx.context, &stream);
            if (result) {
                return result;
            }
        }

        auto result = cuda_do_multidimensional_copy(user_context, c, c.src + c.src_begin, c.dst, dst->dimensions, from_host, to_host, stream);
        if (result) {
            return result;
        }

#ifdef DEBUG_RUNTIME
        uint64_t t_after = halide_current_time_ns(user_context);
        debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
#endif
    }

    return halide_error_code_success;
}

namespace {

WEAK int cuda_device_crop_from_offset(const struct halide_buffer_t *src,
                                      int64_t offset,
                                      struct halide_buffer_t *dst) {
    dst->device = src->device + offset;
    dst->device_interface = src->device_interface;
    dst->set_device_dirty(src->device_dirty());
    return halide_error_code_success;
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
    return halide_error_code_success;
}

WEAK int halide_cuda_copy_to_device(void *user_context, halide_buffer_t *buf) {
    return halide_cuda_buffer_copy(user_context, buf, &cuda_device_interface, buf);
}

WEAK int halide_cuda_copy_to_host(void *user_context, halide_buffer_t *buf) {
    return halide_cuda_buffer_copy(user_context, buf, nullptr, buf);
}

// Used to generate correct timings when tracing
WEAK int halide_cuda_device_sync(void *user_context, struct halide_buffer_t *) {
    debug(user_context)
        << "CUDA: halide_cuda_device_sync (user_context: " << user_context << ")\n";

    Context ctx(user_context);
    if (ctx.error()) {
        return ctx.error();
    }

#ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
#endif

    CUresult err;
    if (cuStreamSynchronize != nullptr) {
        CUstream stream;
        auto result = halide_cuda_get_stream(user_context, ctx.context, &stream);
        if (result) {
            return result;
        }
        err = cuStreamSynchronize(stream);
    } else {
        err = cuCtxSynchronize();
    }
    if (err != CUDA_SUCCESS) {
        return error_cuda(user_context, err, "cuCtxSynchronize failed");
    }

#ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
#endif

    return halide_error_code_success;
}

WEAK int halide_cuda_run(void *user_context,
                         void *state_ptr,
                         const char *entry_name,
                         int blocksX, int blocksY, int blocksZ,
                         int threadsX, int threadsY, int threadsZ,
                         int shared_mem_bytes,
                         size_t arg_sizes[],
                         void *args[],
                         int8_t arg_is_buffer[]) {

    debug(user_context) << "CUDA: halide_cuda_run ("
                        << "user_context: " << user_context << ", "
                        << "entry: " << entry_name << ", "
                        << "blocks: " << blocksX << "x" << blocksY << "x" << blocksZ << ", "
                        << "threads: " << threadsX << "x" << threadsY << "x" << threadsZ << ", "
                        << "shmem: " << shared_mem_bytes << "\n";

    CUresult err;
    Context ctx(user_context);
    if (ctx.error()) {
        return ctx.error();
    }

    debug(user_context) << "Got context.\n";

#ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
#endif

    CUmodule mod{};
    bool found = compilation_cache.lookup(ctx.context, state_ptr, mod);
    halide_abort_if_false(user_context, found && mod != nullptr);

    debug(user_context) << "Got module " << mod << "\n";
    CUfunction f;
    err = cuModuleGetFunction(&f, mod, entry_name);
    debug(user_context) << "Got function " << f << "\n";
    if (err != CUDA_SUCCESS) {
        return error_cuda(user_context, err, "cuModuleGetFunction failed");
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
    void **translated_args = (void **)malloc((num_args + 1) * sizeof(void *));
    uint64_t *dev_handles = (uint64_t *)malloc(num_args * sizeof(uint64_t));
    for (size_t i = 0; i <= num_args; i++) {  // Get nullptr at end.
        if (arg_is_buffer[i]) {
            halide_abort_if_false(user_context, arg_sizes[i] == sizeof(uint64_t));
            dev_handles[i] = ((halide_buffer_t *)args[i])->device;
            translated_args[i] = &(dev_handles[i]);
            debug(user_context) << "    halide_cuda_run translated arg" << (int)i
                                << " [" << (*((void **)translated_args[i])) << " ...]\n";
        } else {
            translated_args[i] = args[i];
        }
    }

    CUstream stream = nullptr;
    // We use whether this routine was defined in the cuda driver library
    // as a test for streams support in the cuda implementation.
    if (cuStreamSynchronize != nullptr) {
        if (auto result = halide_cuda_get_stream(user_context, ctx.context, &stream);
            result != halide_error_code_success) {
            error(user_context) << "CUDA: In halide_cuda_run, halide_cuda_get_stream returned " << result << "\n";
            free(dev_handles);
            free(translated_args);
            return result;
        }
    }

    err = cuLaunchKernel(f,
                         blocksX, blocksY, blocksZ,
                         threadsX, threadsY, threadsZ,
                         shared_mem_bytes,
                         stream,
                         translated_args,
                         nullptr);
    free(dev_handles);
    free(translated_args);
    if (err != CUDA_SUCCESS) {
        return error_cuda(user_context, err, "cuLaunchKernel failed");
    }

#ifdef DEBUG_RUNTIME
    err = cuCtxSynchronize();
    if (err != CUDA_SUCCESS) {
        return error_cuda(user_context, err, "cuCtxSynchronize failed");
    }
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
#endif
    return halide_error_code_success;
}

WEAK int halide_cuda_device_and_host_malloc(void *user_context, struct halide_buffer_t *buf) {
    return halide_default_device_and_host_malloc(user_context, buf, &cuda_device_interface);
}

WEAK int halide_cuda_device_and_host_free(void *user_context, struct halide_buffer_t *buf) {
    return halide_default_device_and_host_free(user_context, buf, &cuda_device_interface);
}

WEAK int halide_cuda_wrap_device_ptr(void *user_context, struct halide_buffer_t *buf, uint64_t device_ptr) {
    halide_abort_if_false(user_context, buf->device == 0);
    if (buf->device != 0) {
        error(user_context) << "halide_cuda_wrap_device_ptr: device field is already non-zero";
        return halide_error_code_generic_error;
    }
    buf->device = device_ptr;
    buf->device_interface = &cuda_device_interface;
    buf->device_interface->impl->use_module();
#ifdef DEBUG_RUNTIME
    if (auto result = validate_device_pointer(user_context, buf);
        result != halide_error_code_success) {
        buf->device_interface->impl->release_module();
        buf->device = 0;
        buf->device_interface = nullptr;
        return result;
    }
#endif
    return halide_error_code_success;
}

WEAK int halide_cuda_detach_device_ptr(void *user_context, struct halide_buffer_t *buf) {
    if (buf->device == 0) {
        return halide_error_code_success;
    }
    halide_abort_if_false(user_context, buf->device_interface == &cuda_device_interface);
    buf->device_interface->impl->release_module();
    buf->device = 0;
    buf->device_interface = nullptr;
    return halide_error_code_success;
}

WEAK uintptr_t halide_cuda_get_device_ptr(void *user_context, struct halide_buffer_t *buf) {
    if (buf->device == 0) {
        return halide_error_code_success;
    }
    halide_abort_if_false(user_context, buf->device_interface == &cuda_device_interface);
    return (uintptr_t)buf->device;
}

WEAK const halide_device_interface_t *halide_cuda_device_interface() {
    return &cuda_device_interface;
}

WEAK int halide_cuda_compute_capability(void *user_context, int *major, int *minor) {
    if (!lib_cuda && !cuInit) {
        // If cuda can't be found, we want to return 0, 0 and it's not
        // considered an error. So we should be very careful about
        // looking for libcuda without tripping any errors in the rest
        // of this runtime.
        void *sym = halide_cuda_get_symbol(user_context, "cuInit");
        if (!sym) {
            *major = *minor = 0;
            return halide_error_code_success;
        }
    }

    {
        Context ctx(user_context);
        if (ctx.error()) {
            return ctx.error();
        }

        CUresult err;

        CUdevice dev;
        err = cuCtxGetDevice(&dev);
        if (err != CUDA_SUCCESS) {
            return error_cuda(user_context, err, "cuCtxGetDevice failed");
        }

        err = cuDeviceGetAttribute(major, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR, dev);
        if (err == CUDA_SUCCESS) {
            err = cuDeviceGetAttribute(minor, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR, dev);
        }

        if (err != CUDA_SUCCESS) {
            return error_cuda(user_context, err, "cuDeviceGetAttribute failed");
        }
    }

    return halide_error_code_success;
}

namespace {
WEAK __attribute__((destructor)) void halide_cuda_cleanup() {
    compilation_cache.release_all(nullptr, cuModuleUnload);
    (void)halide_cuda_device_release(nullptr);  // ignore error
}
}  // namespace

}  // extern "C" linkage

namespace Halide {
namespace Runtime {
namespace Internal {
namespace Cuda {

WEAK const char *get_cuda_error_name(CUresult err) {
    switch (err) {
    case CUDA_SUCCESS:
        return "CUDA_SUCCESS";
    case CUDA_ERROR_INVALID_VALUE:
        return "CUDA_ERROR_INVALID_VALUE";
    case CUDA_ERROR_OUT_OF_MEMORY:
        return "CUDA_ERROR_OUT_OF_MEMORY";
    case CUDA_ERROR_NOT_INITIALIZED:
        return "CUDA_ERROR_NOT_INITIALIZED";
    case CUDA_ERROR_DEINITIALIZED:
        return "CUDA_ERROR_DEINITIALIZED";
    case CUDA_ERROR_PROFILER_DISABLED:
        return "CUDA_ERROR_PROFILER_DISABLED";
    case CUDA_ERROR_PROFILER_NOT_INITIALIZED:
        return "CUDA_ERROR_PROFILER_NOT_INITIALIZED";
    case CUDA_ERROR_PROFILER_ALREADY_STARTED:
        return "CUDA_ERROR_PROFILER_ALREADY_STARTED";
    case CUDA_ERROR_PROFILER_ALREADY_STOPPED:
        return "CUDA_ERROR_PROFILER_ALREADY_STOPPED";
    case CUDA_ERROR_NO_DEVICE:
        return "CUDA_ERROR_NO_DEVICE";
    case CUDA_ERROR_INVALID_DEVICE:
        return "CUDA_ERROR_INVALID_DEVICE";
    case CUDA_ERROR_INVALID_IMAGE:
        return "CUDA_ERROR_INVALID_IMAGE";
    case CUDA_ERROR_INVALID_CONTEXT:
        return "CUDA_ERROR_INVALID_CONTEXT";
    case CUDA_ERROR_CONTEXT_ALREADY_CURRENT:
        return "CUDA_ERROR_CONTEXT_ALREADY_CURRENT";
    case CUDA_ERROR_MAP_FAILED:
        return "CUDA_ERROR_MAP_FAILED";
    case CUDA_ERROR_UNMAP_FAILED:
        return "CUDA_ERROR_UNMAP_FAILED";
    case CUDA_ERROR_ARRAY_IS_MAPPED:
        return "CUDA_ERROR_ARRAY_IS_MAPPED";
    case CUDA_ERROR_ALREADY_MAPPED:
        return "CUDA_ERROR_ALREADY_MAPPED";
    case CUDA_ERROR_NO_BINARY_FOR_GPU:
        return "CUDA_ERROR_NO_BINARY_FOR_GPU";
    case CUDA_ERROR_ALREADY_ACQUIRED:
        return "CUDA_ERROR_ALREADY_ACQUIRED";
    case CUDA_ERROR_NOT_MAPPED:
        return "CUDA_ERROR_NOT_MAPPED";
    case CUDA_ERROR_NOT_MAPPED_AS_ARRAY:
        return "CUDA_ERROR_NOT_MAPPED_AS_ARRAY";
    case CUDA_ERROR_NOT_MAPPED_AS_POINTER:
        return "CUDA_ERROR_NOT_MAPPED_AS_POINTER";
    case CUDA_ERROR_ECC_UNCORRECTABLE:
        return "CUDA_ERROR_ECC_UNCORRECTABLE";
    case CUDA_ERROR_UNSUPPORTED_LIMIT:
        return "CUDA_ERROR_UNSUPPORTED_LIMIT";
    case CUDA_ERROR_CONTEXT_ALREADY_IN_USE:
        return "CUDA_ERROR_CONTEXT_ALREADY_IN_USE";
    case CUDA_ERROR_PEER_ACCESS_UNSUPPORTED:
        return "CUDA_ERROR_PEER_ACCESS_UNSUPPORTED";
    case CUDA_ERROR_INVALID_PTX:
        return "CUDA_ERROR_INVALID_PTX";
    case CUDA_ERROR_INVALID_GRAPHICS_CONTEXT:
        return "CUDA_ERROR_INVALID_GRAPHICS_CONTEXT";
    case CUDA_ERROR_NVLINK_UNCORRECTABLE:
        return "CUDA_ERROR_NVLINK_UNCORRECTABLE";
    case CUDA_ERROR_JIT_COMPILER_NOT_FOUND:
        return "CUDA_ERROR_JIT_COMPILER_NOT_FOUND";
    case CUDA_ERROR_INVALID_SOURCE:
        return "CUDA_ERROR_INVALID_SOURCE";
    case CUDA_ERROR_FILE_NOT_FOUND:
        return "CUDA_ERROR_FILE_NOT_FOUND";
    case CUDA_ERROR_SHARED_OBJECT_SYMBOL_NOT_FOUND:
        return "CUDA_ERROR_SHARED_OBJECT_SYMBOL_NOT_FOUND";
    case CUDA_ERROR_SHARED_OBJECT_INIT_FAILED:
        return "CUDA_ERROR_SHARED_OBJECT_INIT_FAILED";
    case CUDA_ERROR_OPERATING_SYSTEM:
        return "CUDA_ERROR_OPERATING_SYSTEM";
    case CUDA_ERROR_INVALID_HANDLE:
        return "CUDA_ERROR_INVALID_HANDLE";
    case CUDA_ERROR_NOT_FOUND:
        return "CUDA_ERROR_NOT_FOUND";
    case CUDA_ERROR_NOT_READY:
        return "CUDA_ERROR_NOT_READY";
    case CUDA_ERROR_ILLEGAL_ADDRESS:
        return "CUDA_ERROR_ILLEGAL_ADDRESS";
    case CUDA_ERROR_LAUNCH_OUT_OF_RESOURCES:
        return "CUDA_ERROR_LAUNCH_OUT_OF_RESOURCES";
    case CUDA_ERROR_LAUNCH_TIMEOUT:
        return "CUDA_ERROR_LAUNCH_TIMEOUT";
    case CUDA_ERROR_LAUNCH_INCOMPATIBLE_TEXTURING:
        return "CUDA_ERROR_LAUNCH_INCOMPATIBLE_TEXTURING";
    case CUDA_ERROR_PEER_ACCESS_ALREADY_ENABLED:
        return "CUDA_ERROR_PEER_ACCESS_ALREADY_ENABLED";
    case CUDA_ERROR_PEER_ACCESS_NOT_ENABLED:
        return "CUDA_ERROR_PEER_ACCESS_NOT_ENABLED";
    case CUDA_ERROR_PRIMARY_CONTEXT_ACTIVE:
        return "CUDA_ERROR_PRIMARY_CONTEXT_ACTIVE";
    case CUDA_ERROR_CONTEXT_IS_DESTROYED:
        return "CUDA_ERROR_CONTEXT_IS_DESTROYED";
    // A trap instruction produces the below error, which is how we codegen asserts on GPU
    case CUDA_ERROR_ILLEGAL_INSTRUCTION:
        return "Illegal instruction or Halide assertion failure inside kernel";
    case CUDA_ERROR_MISALIGNED_ADDRESS:
        return "CUDA_ERROR_MISALIGNED_ADDRESS";
    case CUDA_ERROR_INVALID_ADDRESS_SPACE:
        return "CUDA_ERROR_INVALID_ADDRESS_SPACE";
    case CUDA_ERROR_INVALID_PC:
        return "CUDA_ERROR_INVALID_PC";
    case CUDA_ERROR_LAUNCH_FAILED:
        return "CUDA_ERROR_LAUNCH_FAILED";
    case CUDA_ERROR_NOT_PERMITTED:
        return "CUDA_ERROR_NOT_PERMITTED";
    case CUDA_ERROR_NOT_SUPPORTED:
        return "CUDA_ERROR_NOT_SUPPORTED";
    case CUDA_ERROR_UNKNOWN:
        return "CUDA_ERROR_UNKNOWN";
    default:
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
    &cuda_device_interface_impl};

}  // namespace Cuda
}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide
