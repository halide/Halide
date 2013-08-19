/*
 * Build LL module with:
 *
 *   clang -I/usr/local/cuda/include -c -S -emit-llvm ptx_shim_tmpl.c -o ptx_shim_tmpl.ll
 */

//#include <cuda.h>
#include <assert.h>
#include "../buffer_t.h"

// The PTX host extends the x86 target
#include "posix_allocator.cpp"
#include "posix_clock.cpp"
#include "posix_error_handler.cpp"
#include "write_debug_image.cpp"
#include "posix_io.cpp"
#include "posix_math.cpp"
#ifdef _WIN32
#include "fake_thread_pool.cpp"
#else
#ifdef __APPLE__
#include "gcd_thread_pool.cpp"
#else
#include "posix_thread_pool.cpp"
#endif
#endif

#define WEAK __attribute__((weak))

extern "C" {

#ifndef DEBUG
#define CHECK_CALL(c,str) c
#define TIME_CALL(c,str) c
#else
//#define CHECK_CALL(c) (assert((c) == CUDA_SUCCESS))
#define CHECK_CALL(c,str) do {\
    fprintf(stderr, "Do %s\n", str); \
    CUresult status = (c); \
    if (status != CUDA_SUCCESS) \
        fprintf(stderr, "CUDA: %s returned non-success: %d\n", str, status); \
    assert(status == CUDA_SUCCESS); \
} while(0)
#define TIME_CALL(c,str) do {\
    cuEventRecord(__start, 0);                              \
    CHECK_CALL((c),(str));                                  \
    cuEventRecord(__end, 0);                                \
    cuEventSynchronize(__end);                              \
    float msec;                                             \
    cuEventElapsedTime(&msec, __start, __end);              \
    printf("   (took %fms, t=%d)\n", msec, halide_current_time());  \
} while(0)
#endif //DEBUG

#ifndef __cuda_cuda_h__
#ifdef _WIN32
#define CUDAAPI __stdcall
#else
#define CUDAAPI
#endif

// API version > 3020
#define cuCtxCreate                         cuCtxCreate_v2
#define cuMemAlloc                          cuMemAlloc_v2
#define cuMemFree                           cuMemFree_v2
#define cuMemcpyHtoD                        cuMemcpyHtoD_v2
#define cuMemcpyDtoH                        cuMemcpyDtoH_v2
// API version >= 4000
#define cuCtxDestroy                        cuCtxDestroy_v2
#define cuCtxPopCurrent                     cuCtxPopCurrent_v2
#define cuCtxPushCurrent                    cuCtxPushCurrent_v2
#define cuStreamDestroy                     cuStreamDestroy_v2
#define cuEventDestroy                      cuEventDestroy_v2

typedef unsigned long long CUdeviceptr; // hard code 64-bits for now
typedef int CUdevice;                                     /**< CUDA device */
typedef struct CUctx_st *CUcontext;                       /**< CUDA context */
typedef struct CUmod_st *CUmodule;                        /**< CUDA module */
typedef struct CUfunc_st *CUfunction;                     /**< CUDA function */
typedef struct CUstream_st *CUstream;                     /**< CUDA stream */
typedef struct CUevent_st *CUevent;                       /**< CUDA event */
typedef enum {
    CUDA_SUCCESS                              = 0,
    CUDA_ERROR_INVALID_VALUE                  = 1,
    CUDA_ERROR_OUT_OF_MEMORY                  = 2,
    CUDA_ERROR_NOT_INITIALIZED                = 3,
    CUDA_ERROR_DEINITIALIZED                  = 4,
    CUDA_ERROR_PROFILER_DISABLED           = 5,
    CUDA_ERROR_PROFILER_NOT_INITIALIZED       = 6,
    CUDA_ERROR_PROFILER_ALREADY_STARTED       = 7,
    CUDA_ERROR_PROFILER_ALREADY_STOPPED       = 8,
    CUDA_ERROR_NO_DEVICE                      = 100,
    CUDA_ERROR_INVALID_DEVICE                 = 101,
    CUDA_ERROR_INVALID_IMAGE                  = 200,
    CUDA_ERROR_INVALID_CONTEXT                = 201,
    CUDA_ERROR_CONTEXT_ALREADY_CURRENT        = 202,
    CUDA_ERROR_MAP_FAILED                     = 205,
    CUDA_ERROR_UNMAP_FAILED                   = 206,
    CUDA_ERROR_ARRAY_IS_MAPPED                = 207,
    CUDA_ERROR_ALREADY_MAPPED                 = 208,
    CUDA_ERROR_NO_BINARY_FOR_GPU              = 209,
    CUDA_ERROR_ALREADY_ACQUIRED               = 210,
    CUDA_ERROR_NOT_MAPPED                     = 211,
    CUDA_ERROR_NOT_MAPPED_AS_ARRAY            = 212,
    CUDA_ERROR_NOT_MAPPED_AS_POINTER          = 213,
    CUDA_ERROR_ECC_UNCORRECTABLE              = 214,
    CUDA_ERROR_UNSUPPORTED_LIMIT              = 215,
    CUDA_ERROR_CONTEXT_ALREADY_IN_USE         = 216,
    CUDA_ERROR_INVALID_SOURCE                 = 300,
    CUDA_ERROR_FILE_NOT_FOUND                 = 301,
    CUDA_ERROR_SHARED_OBJECT_SYMBOL_NOT_FOUND = 302,
    CUDA_ERROR_SHARED_OBJECT_INIT_FAILED      = 303,
    CUDA_ERROR_OPERATING_SYSTEM               = 304,
    CUDA_ERROR_INVALID_HANDLE                 = 400,
    CUDA_ERROR_NOT_FOUND                      = 500,
    CUDA_ERROR_NOT_READY                      = 600,
    CUDA_ERROR_LAUNCH_FAILED                  = 700,
    CUDA_ERROR_LAUNCH_OUT_OF_RESOURCES        = 701,
    CUDA_ERROR_LAUNCH_TIMEOUT                 = 702,
    CUDA_ERROR_LAUNCH_INCOMPATIBLE_TEXTURING  = 703,
    CUDA_ERROR_PEER_ACCESS_ALREADY_ENABLED = 704,
    CUDA_ERROR_PEER_ACCESS_NOT_ENABLED    = 705,
    CUDA_ERROR_PRIMARY_CONTEXT_ACTIVE         = 708,
    CUDA_ERROR_CONTEXT_IS_DESTROYED           = 709,
    CUDA_ERROR_UNKNOWN                        = 999
} CUresult;

#define CU_POINTER_ATTRIBUTE_CONTEXT 1

CUresult CUDAAPI cuInit(unsigned int Flags);
CUresult CUDAAPI cuDeviceGetCount(int *count);
CUresult CUDAAPI cuDeviceGet(CUdevice *device, int ordinal);
CUresult CUDAAPI cuCtxCreate(CUcontext *pctx, unsigned int flags, CUdevice dev);
CUresult CUDAAPI cuCtxDestroy(CUcontext pctx);
CUresult CUDAAPI cuModuleLoadData(CUmodule *module, const void *image);
CUresult CUDAAPI cuModuleUnload(CUmodule module);
CUresult CUDAAPI cuModuleGetFunction(CUfunction *hfunc, CUmodule hmod, const char *name);
CUresult CUDAAPI cuMemAlloc(CUdeviceptr *dptr, size_t bytesize);
CUresult CUDAAPI cuMemFree(CUdeviceptr dptr);
CUresult CUDAAPI cuMemcpyHtoD(CUdeviceptr dstDevice, const void *srcHost, size_t ByteCount);
CUresult CUDAAPI cuMemcpyDtoH(void *dstHost, CUdeviceptr srcDevice, size_t ByteCount);
CUresult CUDAAPI cuLaunchKernel(CUfunction f,
                                unsigned int gridDimX,
                                unsigned int gridDimY,
                                unsigned int gridDimZ,
                                unsigned int blockDimX,
                                unsigned int blockDimY,
                                unsigned int blockDimZ,
                                unsigned int sharedMemBytes,
                                CUstream hStream,
                                void **kernelParams,
                                void **extra);
CUresult CUDAAPI cuCtxSynchronize();

CUresult CUDAAPI cuCtxPushCurrent(CUcontext ctx);
CUresult CUDAAPI cuCtxPopCurrent(CUcontext *pctx);

CUresult CUDAAPI cuEventRecord(CUevent hEvent, CUstream hStream);
CUresult CUDAAPI cuEventCreate(CUevent *phEvent, unsigned int Flags);
CUresult CUDAAPI cuEventDestroy(CUevent phEvent);
CUresult CUDAAPI cuEventSynchronize(CUevent hEvent);
CUresult CUDAAPI cuEventElapsedTime(float *pMilliseconds, CUevent hStart, CUevent hEnd);
CUresult CUDAAPI cuPointerGetAttribute(void *result, int query, CUdeviceptr ptr);

#endif //__cuda_cuda_h__

// Device, Context, Module, and Function for this entrypoint are tracked locally
// and constructed lazily on first run.
// TODO: make __f, __mod into arrays?
// static vector<CUfunction> __f;
}
extern "C" {
// A cuda context defined in this module with weak linkage
CUcontext WEAK weak_cuda_ctx = 0;

// A pointer to the cuda context to use, which may not be the one above. This pointer is followed at init_kernels time.
CUcontext WEAK *cuda_ctx_ptr = NULL;

WEAK void halide_set_cuda_context(CUcontext *ctx_ptr) {
    cuda_ctx_ptr = ctx_ptr;
}

static CUmodule __mod;
static CUevent __start, __end;

/*
// Used to create buffer_ts to track internal allocations caused by our runtime
// TODO: look into cuMemAllocHost for page-locked host memory, allowing easy transfer?
// TODO: make Buffer args typed, so elem_size can be statically inferred?
static buffer_t* __make_buffer(uint8_t* host, size_t elem_size,
                        size_t dim0, size_t dim1,
                        size_t dim2, size_t dim3)
{
    buffer_t* buf = (buffer_t*)malloc(sizeof(buffer_t));
    buf->host = host;
    buf->dev = 0;
    buf->extent[0] = dim0;
    buf->extent[1] = dim1;
    buf->extent[2] = dim2;
    buf->extent[3] = dim3;
    buf->elem_size = elem_size;
    buf->host_dirty = false;
    buf->dev_dirty = false;
    return buf;
}

static void __release_buffer(buffer_t* buf) {
    free(buf);
}

static buffer_t* __malloc_buffer(int32_t size) {
    return __make_buffer((uint8_t*)malloc(size), sizeof(uint8_t), size, 1, 1, 1);
}

*/

WEAK bool halide_validate_dev_pointer(buffer_t* buf) {
    CUcontext ctx;
    CUresult result = cuPointerGetAttribute(&ctx, CU_POINTER_ATTRIBUTE_CONTEXT, buf->dev);
    if (result) {
        fprintf(stderr, "Bad device pointer %p: cuPointerGetAttribute returned %d\n", (void *)buf->dev, result);
        return false;
    }
    return true;
}

WEAK void halide_dev_free(buffer_t* buf) {

    #ifdef DEBUG
    fprintf(stderr, "In dev_free of %p - dev: 0x%p\n", buf, (void*)buf->dev);
    assert(halide_validate_dev_pointer(buf));
    #endif

    CHECK_CALL( cuMemFree(buf->dev), "cuMemFree" );
    buf->dev = 0;

}

WEAK void halide_init_kernels(const char* ptx_src) {
    // If the context pointer isn't hooked up yet, point it at this module's weak-linkage context.
    if (cuda_ctx_ptr == NULL) {
        cuda_ctx_ptr = &weak_cuda_ctx;
    }

    // Initialize one shared context for all Halide compiled instances
    if (*cuda_ctx_ptr == 0) {
        // Initialize CUDA
        CHECK_CALL( cuInit(0), "cuInit" );

        // Make sure we have a device
        int deviceCount = 0;
        CHECK_CALL( cuDeviceGetCount(&deviceCount), "cuDeviceGetCount" );
        assert(deviceCount > 0);

        char *device_str = getenv("HL_GPU_DEVICE");

        CUdevice dev;
        // Get device
        CUresult status;
        if (device_str) {
            status = cuDeviceGet(&dev, atoi(device_str));
        } else {
            for (int id = 2; id >= 0; id--) {
                // Try to get a device >0 first, since 0 should be our display device
                status = cuDeviceGet(&dev, id);
                if (status == CUDA_SUCCESS) break;
            }
        }

        if (status != CUDA_SUCCESS) {
            fprintf(stderr, "Failed to get device\n");
            exit(-1);
        }

        #ifdef DEBUG
        fprintf(stderr, "Got device %d, about to create context (t=%d)\n", dev, halide_current_time());
        #endif


        // Create context
        CHECK_CALL( cuCtxCreate(cuda_ctx_ptr, 0, dev), "cuCtxCreate" );

    } else {
        //CHECK_CALL( cuCtxPushCurrent(*cuda_ctx_ptr), "cuCtxPushCurrent" );
    }

    // Initialize a module for just this Halide module
    if (!__mod) {
        // Create module
        CHECK_CALL( cuModuleLoadData(&__mod, ptx_src), "cuModuleLoadData" );

        #ifdef DEBUG
        fprintf(stderr, "-------\nCompiling PTX:\n%s\n--------\n", ptx_src);
        #endif
    }

    // Create two events for timing
    if (!__start) {
        cuEventCreate(&__start, 0);
        cuEventCreate(&__end, 0);
    }
}

#ifdef DEBUG
#define CHECK_CALL_DEINIT_OK(c,str) do {\
    fprintf(stderr, "Do %s\n", str); \
    CUresult status = (c); \
    if (status != CUDA_SUCCESS && status != CUDA_ERROR_DEINITIALIZED) \
        fprintf(stderr, "CUDA: %s returned non-success: %d\n", str, status); \
    assert(status == CUDA_SUCCESS || status == CUDA_ERROR_DEINITIALIZED); \
} while(0)
#else
#define CHECK_CALL_DEINIT_OK(c,str) c
#endif

WEAK void halide_release() {
    // It's possible that this is being called from the destructor of
    // a static variable, in which case the driver may already be
    // shutting down. For this reason we allow the deinitialized
    // error.
    CHECK_CALL_DEINIT_OK( cuCtxSynchronize(), "cuCtxSynchronize on exit" );

    // Only destroy the context if we own it
    if (weak_cuda_ctx) {
        CHECK_CALL_DEINIT_OK( cuCtxDestroy(weak_cuda_ctx), "cuCtxDestroy on exit" );
        weak_cuda_ctx = 0;
    }

    // Destroy the events
    if (__start) {
        cuEventDestroy(__start);
        cuEventDestroy(__end);
        __start = __end = 0;
    }

    // Unload the module
    if (__mod) {
        CHECK_CALL_DEINIT_OK( cuModuleUnload(__mod), "cuModuleUnload" );
        __mod = 0;
    }

    //CHECK_CALL( cuCtxPopCurrent(&ignore), "cuCtxPopCurrent" );
}

static CUfunction __get_kernel(const char* entry_name)
{
    CUfunction f;

    #ifdef DEBUG
    char msg[256];
    snprintf(msg, 256, "get_kernel %s (t=%d)", entry_name, halide_current_time() );
    #endif

    // Get kernel function ptr
    TIME_CALL( cuModuleGetFunction(&f, __mod, entry_name), msg );

    return f;
}

static inline size_t buf_size(buffer_t* buf) {
    size_t sz = buf->elem_size;
    if (buf->extent[0]) sz *= buf->extent[0];
    if (buf->extent[1]) sz *= buf->extent[1];
    if (buf->extent[2]) sz *= buf->extent[2];
    if (buf->extent[3]) sz *= buf->extent[3];
    assert(sz);
    return sz;
}

WEAK void halide_dev_malloc(buffer_t* buf) {
    if (buf->dev) {
        // This buffer already has a device allocation
        return;
    }

    size_t size = buf_size(buf);

    #ifdef DEBUG
    fprintf(stderr, "dev_malloc allocating buffer of %zd bytes, %zdx%zdx%zdx%zd (%d bytes per element)\n",
            size, buf->extent[0], buf->extent[1], buf->extent[2], buf->extent[3], buf->elem_size);
    #endif

    CUdeviceptr p;
    TIME_CALL( cuMemAlloc(&p, size), "dev_malloc");
    buf->dev = (uint64_t)p;
    assert(buf->dev);

    #ifdef DEBUG
    assert(halide_validate_dev_pointer(buf));
    #endif
}

WEAK void halide_copy_to_dev(buffer_t* buf) {
    if (buf->host_dirty) {
        assert(buf->host && buf->dev);
        size_t size = buf_size(buf);
        #ifdef DEBUG
        char msg[256];
        snprintf(msg, 256, "copy_to_dev (%zu bytes) %p -> %p (t=%d)", size, buf->host, (void*)buf->dev, halide_current_time() );
        assert(halide_validate_dev_pointer(buf));
        #endif
        TIME_CALL( cuMemcpyHtoD(buf->dev, buf->host, size), msg );
    }
    buf->host_dirty = false;
}

WEAK void halide_copy_to_host(buffer_t* buf) {
    if (buf->dev_dirty) {
        assert(buf->dev);
        assert(buf->host);
        size_t size = buf_size(buf);
        #ifdef DEBUG
        char msg[256];
        snprintf(msg, 256, "copy_to_host (%zu bytes) %p -> %p", size, (void*)buf->dev, buf->host );
        assert(halide_validate_dev_pointer(buf));
        #endif
        TIME_CALL( cuMemcpyDtoH(buf->host, buf->dev, size), msg );
    }
    buf->dev_dirty = false;
}
#define _COPY_TO_HOST

// Used to generate correct timings when tracing
WEAK void halide_dev_sync() {
    cuCtxSynchronize();
}

WEAK void halide_dev_run(
    const char* entry_name,
    int blocksX, int blocksY, int blocksZ,
    int threadsX, int threadsY, int threadsZ,
    int shared_mem_bytes,
    size_t arg_sizes[],
    void* args[])
{
    CUfunction f = __get_kernel(entry_name);

    #ifdef DEBUG
    char msg[256];
    snprintf(
        msg, 256,
        "dev_run %s with (%dx%dx%d) blks, (%dx%dx%d) threads, %d shmem (t=%d)",
        entry_name, blocksX, blocksY, blocksZ, threadsX, threadsY, threadsZ, shared_mem_bytes,
        halide_current_time()
    );
    #endif

    TIME_CALL(
        cuLaunchKernel(
            f,
            blocksX,  blocksY,  blocksZ,
            threadsX, threadsY, threadsZ,
            shared_mem_bytes,
            NULL, // stream
            args,
            NULL
        ),
        msg
    );
}

} // extern "C" linkage
