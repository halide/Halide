#include "runtime_internal.h"
#include "../buffer_t.h"
#include "HalideRuntime.h"
#include "mini_cuda.h"
#include "cuda_opencl_shared.h"

namespace Halide { namespace Runtime { namespace Internal {

WEAK const char *get_cuda_error_name(CUresult error);
WEAK CUresult create_cuda_context(void *user_context, CUcontext *ctx);

// A cuda context defined in this module with weak linkage
CUcontext WEAK weak_cuda_ctx = 0;
volatile int WEAK weak_cuda_lock = 0;

// A pointer to the cuda context to use, which may not be the one above. This pointer is followed at init_kernels time.
CUcontext WEAK *cuda_ctx_ptr = NULL;
volatile int WEAK *cuda_lock_ptr = NULL;

}}} // namespace Halide::Runtime::Internal

extern "C" {

extern int atoi(const char *);
extern char *getenv(const char *);
extern int64_t halide_current_time_ns(void *user_context);
extern void *malloc(size_t);

WEAK void halide_set_cuda_context(CUcontext *ctx_ptr, volatile int *lock_ptr) {
    cuda_ctx_ptr = ctx_ptr;
    cuda_lock_ptr = lock_ptr;
}

// The default implementation of halide_acquire_cl_context uses the global
// pointers above, and serializes access with a spin lock.
// Overriding implementations of acquire/release must implement the following
// behavior:
// - halide_acquire_cl_context should always store a valid context/command
//   queue in ctx/q, or return an error code.
// - A call to halide_acquire_cl_context is followed by a matching call to
//   halide_release_cl_context. halide_acquire_cl_context should block while a
//   previous call (if any) has not yet been released via halide_release_cl_context.
WEAK int halide_acquire_cuda_context(void *user_context, CUcontext *ctx) {
    // TODO: Should we use a more "assertive" assert? these asserts do
    // not block execution on failure.
    halide_assert(user_context, ctx != NULL);

    if (cuda_ctx_ptr == NULL) {
        cuda_ctx_ptr = &weak_cuda_ctx;
        cuda_lock_ptr = &weak_cuda_lock;
    }

    halide_assert(user_context, cuda_lock_ptr != NULL);
    while (__sync_lock_test_and_set(cuda_lock_ptr, 1)) { }

    // If the context has not been initialized, initialize it now.
    halide_assert(user_context, cuda_ctx_ptr != NULL);
    if (*cuda_ctx_ptr == NULL) {
        CUresult error = create_cuda_context(user_context, cuda_ctx_ptr);
        if (error != CUDA_SUCCESS) {
            __sync_lock_release(cuda_lock_ptr);
            return error;
        }
    }

    *ctx = *cuda_ctx_ptr;
    return 0;
}

WEAK int halide_release_cuda_context(void *user_context) {
    __sync_lock_release(cuda_lock_ptr);
    return 0;
}

}

namespace Halide { namespace Runtime { namespace Internal {

// Helper object to acquire and release the cuda context.
class CudaContext {
    void *user_context;

public:
    CUcontext context;
    int error;

    // Constructor sets 'error' if any occurs.
    CudaContext(void *user_context) : user_context(user_context),
                                      context(NULL),
                                      error(CUDA_SUCCESS) {
        error = halide_acquire_cuda_context(user_context, &context);
        halide_assert(user_context, context != NULL);
        if (error != 0) {
            return;
        }

        error = cuCtxPushCurrent(context);
    }

    ~CudaContext() {
        CUcontext old;
        cuCtxPopCurrent(&old);

        halide_release_cuda_context(user_context);
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
                            << get_cuda_error_name(err);
        return err;
    }

    // Make sure we have a device
    int deviceCount = 0;
    err = cuDeviceGetCount(&deviceCount);
    if (err != CUDA_SUCCESS) {
        error(user_context) << "CUDA: cuGetDeviceCount failed: "
                            << get_cuda_error_name(err);
        return err;
    }
    if (deviceCount <= 0) {
        halide_error(user_context, "CUDA: No devices available");
        return CUDA_ERROR_NO_DEVICE;
    }

    int device = halide_get_gpu_device(user_context);
    if (device == -1) {
        device = deviceCount - 1;
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
                                << get_cuda_error_name(err);
            return err;
        }

        size_t memory = 0;
        err = cuDeviceTotalMem(&memory, dev);
        debug(user_context) << "      total memory: " << (memory >> 20) << " MB\n";

        if (err != CUDA_SUCCESS) {
            error(user_context) << "CUDA: cuDeviceTotalMem failed: "
                                << get_cuda_error_name(err);
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
                    << get_cuda_error_name(err)
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
        debug(user_context) << get_cuda_error_name(err) << "\n";
        error(user_context) << "CUDA: cuCtxCreate failed: "
                            << get_cuda_error_name(err);
        return err;
    } else {
        unsigned int version = 0;
        cuCtxGetApiVersion(*ctx, &version);
        debug(user_context) << *ctx << "(" << version << ")\n";
    }

    return CUDA_SUCCESS;
}


WEAK bool validate_dev_pointer(void *user_context, buffer_t* buf, size_t size=0) {
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
    if (buf->dev == 0)
        return true;

    CUcontext ctx;
    CUresult result = cuPointerGetAttribute(&ctx, CU_POINTER_ATTRIBUTE_CONTEXT, buf->dev);
    if (result) {
        error(user_context) << "Bad device pointer " << (void *)buf->dev
                            << ": cuPointerGetAttribute returned "
                            << get_cuda_error_name(result);
        return false;
    }
    return true;
#endif
}
}}} // namespace Halide::Runtime::Internal

extern "C" {
WEAK int halide_init_kernels(void *user_context, void **state_ptr, const char* ptx_src, int size) {
    debug(user_context) << "CUDA: halide_init_kernels (user_context: " << user_context
                        << ", state_ptr: " << state_ptr
                        << ", ptx_src: " << (void *)ptx_src
                        << ", size: " << size << "\n";

    CudaContext ctx(user_context);
    if (ctx.error != 0) {
        return ctx.error;
    }

    #ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
    #endif

    // Create the state object if necessary. This only happens once, regardless
    // of how many times halide_init_kernels/halide_release is called.
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
        CUresult err = cuModuleLoadData(&(*state)->module, ptx_src);
        if (err != CUDA_SUCCESS) {
            debug(user_context) << get_cuda_error_name(err) << "\n";
            error(user_context) << "CUDA: cuModuleLoadData failed: "
                                << get_cuda_error_name(err);
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

WEAK int halide_dev_free(void *user_context, buffer_t* buf) {
    // halide_dev_free, at present, can be exposed to clients and they
    // should be allowed to call halide_dev_free on any buffer_t
    // including ones that have never been used with a GPU.
    if (buf->dev == 0) {
        return 0;
    }

    debug(user_context)
        <<  "CUDA: halide_dev_free (user_context: " << user_context
        << ", buf: " << buf << ")\n";

    CudaContext ctx(user_context);
    if (ctx.error != CUDA_SUCCESS)
        return ctx.error;

    #ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
    #endif

    halide_assert(user_context, validate_dev_pointer(user_context, buf));

    debug(user_context) <<  "    cuMemFree " << (void *)(buf->dev) << "\n";
    CUresult err = cuMemFree(buf->dev);
    // If cuMemFree fails, it isn't likely to succeed later, so just drop
    // the reference.
    buf->dev = 0;
    if (err != CUDA_SUCCESS) {
        error(user_context) << "CUDA: cuMemFree failed: "
                            << get_cuda_error_name(err);
        return err;
    }

    #ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
    #endif

    return 0;
}

WEAK void halide_release(void *user_context) {
    debug(user_context)
        << "CUDA: halide_release (user_context: " <<  user_context << ")\n";

    int err;
    CUcontext ctx;
    err = halide_acquire_cuda_context(user_context, &ctx);
    if (err != CUDA_SUCCESS || !ctx) {
        return;
    }

    // It's possible that this is being called from the destructor of
    // a static variable, in which case the driver may already be
    // shutting down.
    err = cuCtxSynchronize();
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

    // Only destroy the context if we own it
    if (ctx == weak_cuda_ctx) {
        debug(user_context) << "    cuCtxDestroy " << weak_cuda_ctx << "\n";
        err = cuCtxDestroy(weak_cuda_ctx);
        halide_assert(user_context, err == CUDA_SUCCESS || err == CUDA_ERROR_DEINITIALIZED);
        weak_cuda_ctx = NULL;
    }

    halide_release_cuda_context(user_context);
}

WEAK int halide_dev_malloc(void *user_context, buffer_t *buf) {
    debug(user_context)
        << "CUDA: halide_dev_malloc (user_context: " << user_context
        << ", buf: " << buf << ")\n";

    CudaContext ctx(user_context);
    if (ctx.error != CUDA_SUCCESS) {
        return ctx.error;
    }

    size_t size = buf_size(user_context, buf);
    if (buf->dev) {
        // This buffer already has a device allocation
        halide_assert(user_context, validate_dev_pointer(user_context, buf, size));
        return 0;
    }

    halide_assert(user_context, buf->stride[0] >= 0 && buf->stride[1] >= 0 &&
                                buf->stride[2] >= 0 && buf->stride[3] >= 0);

    debug(user_context) << "    allocating buffer of " << size << " bytes, "
                        << "extents: "
                        << buf->extent[0] << "x"
                        << buf->extent[1] << "x"
                        << buf->extent[2] << "x"
                        << buf->extent[3] << " "
                        << "strides: "
                        << buf->stride[0] << "x"
                        << buf->stride[1] << "x"
                        << buf->stride[2] << "x"
                        << buf->stride[3] << " "
                        << "(" << buf->elem_size << " bytes per element)\n";

    #ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
    #endif

    CUdeviceptr p;
    debug(user_context) << "    cuMemAlloc " << size << " -> ";
    CUresult err = cuMemAlloc(&p, size);
    if (err != CUDA_SUCCESS) {
        debug(user_context) << get_cuda_error_name(err) << "\n";
        error(user_context) << "CUDA: cuMemAlloc failed: "
                            << get_cuda_error_name(err);
        return err;
    } else {
        debug(user_context) << (void *)p << "\n";
    }
    halide_assert(user_context, p);
    buf->dev = (uint64_t)p;

    #ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
    #endif

    return 0;
}

WEAK int halide_copy_to_dev(void *user_context, buffer_t* buf) {
    int err = halide_dev_malloc(user_context, buf);
    if (err) {
        return err;
    }

    debug(user_context)
        <<  "CUDA: halide_copy_to_dev (user_context: " << user_context
        << ", buf: " << buf << ")\n";

    CudaContext ctx(user_context);
    if (ctx.error != CUDA_SUCCESS) {
        return ctx.error;
    }

    if (buf->host_dirty) {
        #ifdef DEBUG_RUNTIME
        uint64_t t_before = halide_current_time_ns(user_context);
        #endif

        halide_assert(user_context, buf->host && buf->dev);
        halide_assert(user_context, validate_dev_pointer(user_context, buf));

        dev_copy c = make_host_to_dev_copy(buf);

        for (int w = 0; w < c.extent[3]; w++) {
            for (int z = 0; z < c.extent[2]; z++) {
                for (int y = 0; y < c.extent[1]; y++) {
                    for (int x = 0; x < c.extent[0]; x++) {
                        uint64_t off = (x * c.stride_bytes[0] +
                                        y * c.stride_bytes[1] +
                                        z * c.stride_bytes[2] +
                                        w * c.stride_bytes[3]);
                        void *src = (void *)(c.src + off);
                        CUdeviceptr dst = (CUdeviceptr)(c.dst + off);
                        uint64_t size = c.chunk_size;
                        debug(user_context) << "    cuMemcpyHtoD "
                                            << "(" << x << ", " << y << ", " << z << ", " << w << "), "
                                            << src << " -> " << (void *)dst << ", " << size << " bytes\n";
                        CUresult err = cuMemcpyHtoD(dst, src, size);
                        if (err != CUDA_SUCCESS) {
                            error(user_context) << "CUDA: cuMemcpyHtoD failed: "
                                                << get_cuda_error_name(err);
                            return err;
                        }
                    }
                }
            }
        }


        #ifdef DEBUG_RUNTIME
        uint64_t t_after = halide_current_time_ns(user_context);
        debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
        #endif
    }
    buf->host_dirty = false;
    return 0;
}

WEAK int halide_copy_to_host(void *user_context, buffer_t* buf) {
    if (!buf->dev_dirty) {
        return 0;
    }

    debug(user_context)
        << "CUDA: halide_copy_to_host (user_context: " << user_context
        << ", buf: " << buf << ")\n";

    CudaContext ctx(user_context);
    if (ctx.error != CUDA_SUCCESS) {
        return ctx.error;
    }

    // Need to check dev_dirty again, in case another thread did the
    // copy_to_host before the serialization point above.
    if (buf->dev_dirty) {
        #ifdef DEBUG_RUNTIME
        uint64_t t_before = halide_current_time_ns(user_context);
        #endif

        halide_assert(user_context, buf->dev && buf->dev);
        halide_assert(user_context, validate_dev_pointer(user_context, buf));

        dev_copy c = make_dev_to_host_copy(buf);

        for (int w = 0; w < c.extent[3]; w++) {
            for (int z = 0; z < c.extent[2]; z++) {
                for (int y = 0; y < c.extent[1]; y++) {
                    for (int x = 0; x < c.extent[0]; x++) {
                        uint64_t off = (x * c.stride_bytes[0] +
                                        y * c.stride_bytes[1] +
                                        z * c.stride_bytes[2] +
                                        w * c.stride_bytes[3]);
                        CUdeviceptr src = (CUdeviceptr)(c.src + off);
                        void *dst = (void *)(c.dst + off);
                        uint64_t size = c.chunk_size;

                        debug(user_context) << "    cuMemcpyDtoH "
                                            << "(" << x << ", " << y << ", " << z << ", " << w << "), "
                                            << (void *)src << " -> " << dst << ", " << size << " bytes\n";

                        CUresult err = cuMemcpyDtoH(dst, src, size);
                        if (err != CUDA_SUCCESS) {
                            error(user_context) << "CUDA: cuMemcpyDtoH failed: "
                                                << get_cuda_error_name(err);
                            return err;
                        }
                    }
                }
            }
        }

        #ifdef DEBUG_RUNTIME
        uint64_t t_after = halide_current_time_ns(user_context);
        debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
        #endif
    }
    buf->dev_dirty = false;
    return 0;
}

// Used to generate correct timings when tracing
WEAK int halide_dev_sync(void *user_context) {
    debug(user_context)
        << "CUDA: halide_dev_sync (user_context: " << user_context << ")\n";

    CudaContext ctx(user_context);
    if (ctx.error != CUDA_SUCCESS) {
        return ctx.error;
    }

    #ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
    #endif

    CUresult err = cuCtxSynchronize();
    if (err != CUDA_SUCCESS) {
        error(user_context) << "CUDA: cuCtxSynchronize failed: "
                            << get_cuda_error_name(err);
        return err;
    }

    #ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
    #endif

    return 0;
}

WEAK int halide_dev_run(void *user_context,
                        void *state_ptr,
                        const char* entry_name,
                        int blocksX, int blocksY, int blocksZ,
                        int threadsX, int threadsY, int threadsZ,
                        int shared_mem_bytes,
                        size_t arg_sizes[],
                        void* args[],
                        char** attribute_names,
                        int num_attributes,
                        float** coords_per_dim,
                        int num_coords_dim0,
                        int num_coords_dim1) {

    debug(user_context) << "CUDA: halide_dev_run ("
                        << "user_context: " << user_context << ", "
                        << "entry: " << entry_name << ", "
                        << "blocks: " << blocksX << "x" << blocksY << "x" << blocksZ << ", "
                        << "threads: " << threadsX << "x" << threadsY << "x" << threadsZ << ", "
                        << "shmem: " << shared_mem_bytes << "\n";

    CUresult err;
    CudaContext ctx(user_context);
    if (ctx.error != CUDA_SUCCESS) {
        return ctx.error;
    }

    #ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
    #endif

    halide_assert(user_context, state_ptr);
    CUmodule mod = ((module_state*)state_ptr)->module;
    halide_assert(user_context, mod);
    CUfunction f;
    err = cuModuleGetFunction(&f, mod, entry_name);
    if (err != CUDA_SUCCESS) {
        error(user_context) << "CUDA: cuModuleGetFunction failed: "
                            << get_cuda_error_name(err);
        return err;
    }

    err = cuLaunchKernel(f,
                         blocksX,  blocksY,  blocksZ,
                         threadsX, threadsY, threadsZ,
                         shared_mem_bytes,
                         NULL, // stream
                         args,
                         NULL);
    if (err != CUDA_SUCCESS) {
        error(user_context) << "CUDA: cuLaunchKernel failed: "
                            << get_cuda_error_name(err);
        return err;
    }

    #ifdef DEBUG_RUNTIME
    err = cuCtxSynchronize();
    if (err != CUDA_SUCCESS) {
        error(user_context) << "CUDA: cuCtxSynchronize failed: "
                            << get_cuda_error_name(err);
        return err;
    }
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
    #endif
    return 0;
}

} // extern "C" linkage

namespace Halide { namespace Runtime { namespace Internal {

WEAK const char *get_cuda_error_name(CUresult error) {
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
    default: return "<Unknown error>";
    }
}

}}} // namespace Halide::Runtime::Internal
