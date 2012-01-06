/*
 * Build LL module with:
 *
 *   clang -I/usr/local/cuda/include -c -S -emit-llvm ptx_shim_tmpl.c -o ptx_shim_tmpl.ll
 */
#include <stdio.h>
#include <stdlib.h>
//#include <cuda.h>
#include <assert.h>
#include "buffer.h"

extern "C" {

//#define CHECK_CALL(c) (assert((c) == CUDA_SUCCESS))
//#define CHECK_CALL(c) (success &&= ((c) == CUDA_SUCCESS))
// #define CHECK_CALL(c,str) (c)
#define CHECK_CALL(c,str) {\
    fprintf(stderr, "Do %s\n", str); \
    CUresult status = (c); \
    if (status != CUDA_SUCCESS) \
        fprintf(stderr, "CUDA: %s returned non-success: %d\n", str, status); \
    assert(status == CUDA_SUCCESS); \
    } (c)

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


CUresult CUDAAPI cuInit(unsigned int Flags);
CUresult CUDAAPI cuDeviceGetCount(int *count);
CUresult CUDAAPI cuDeviceGet(CUdevice *device, int ordinal);
CUresult CUDAAPI cuCtxCreate(CUcontext *pctx, unsigned int flags, CUdevice dev);
CUresult CUDAAPI cuModuleLoadData(CUmodule *module, const void *image);
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
#endif //__cuda_cuda_h__

// Device, Context, Module, and Function for this entrypoint are tracked locally
// and constructed lazily on first run.
static CUdevice __dev = 0;
static CUcontext __ctx = 0;
// TODO: make __f, __mod into arrays?
// static vector<CUfunction> __f;
static CUmodule __mod;

// Used to create buffer_ts to track internal allocations caused by our runtime
// TODO: look into cuMemAllocHost for page-locked host memory, allowing easy transfer?
// TODO: make Buffer args typed, so elem_size can be statically inferred?
buffer_t* __make_buffer(uint8_t* host, size_t elem_size,
                        size_t dim0, size_t dim1,
                        size_t dim2, size_t dim3)
{
    buffer_t* buf = (buffer_t*)malloc(sizeof(buffer_t));
    buf->host = host;
    buf->dev = 0;
    buf->dims[0] = dim0;
    buf->dims[1] = dim1;
    buf->dims[2] = dim2;
    buf->dims[3] = dim3;
    buf->elem_size = elem_size;
    buf->host_dirty = true;
    buf->dev_dirty = false;
    return buf;
}

void __release_buffer(buffer_t* buf)
{
    free(buf);
}
buffer_t* __malloc_buffer(int32_t size)
{
    return __make_buffer((uint8_t*)malloc(size), sizeof(uint8_t), size, 1, 1, 1);
}

void __free_buffer(buffer_t* buf)
{
    assert(buf->host);
    free(buf->host);
    if (buf->dev) cuMemFree(buf->dev);
    __release_buffer(buf);
}

void __init(const char* ptx_src)
{
    if (!__dev) {
        // Initialize CUDA
        CHECK_CALL( cuInit(0), "cuInit" );

        // Make sure we have a device
        int deviceCount = 0;
        CHECK_CALL( cuDeviceGetCount(&deviceCount), "cuDeviceGetCount" );
        assert(deviceCount > 0);
        
        // Get device
        CHECK_CALL( cuDeviceGet(&__dev, 0), "cuDeviceGet" );
        
        fprintf(stderr, "Got device %d, about to create context\n", __dev);

        // Create context
        CHECK_CALL( cuCtxCreate(&__ctx, 0, __dev), "cuCtxCreate" );

        // Create module
        CHECK_CALL( cuModuleLoadData(&__mod, ptx_src), "cuModuleLoadData" );

        fprintf(stderr, "-------\nCompiling PTX:\n%s\n--------\n", ptx_src);
    } else {
        CHECK_CALL( cuCtxPushCurrent(__ctx), "cuCtxPushCurrent" );
    }
}

void __release() {
    CUcontext ignore;
    CHECK_CALL( cuCtxPopCurrent(&ignore), "cuCtxPopCurrent" );
}

CUfunction __get_kernel(const char* entry_name)
{
    CUfunction f;
    char msg[256];
    snprintf(msg, 256, "get_kernel %s", entry_name );
    // Get kernel function ptr
    CHECK_CALL( cuModuleGetFunction(&f, __mod, entry_name), msg );
    return f;
}

CUdeviceptr __dev_malloc(size_t bytes) {
    CUdeviceptr p;
    char msg[256];
    snprintf(msg, 256, "dev_malloc (%zu bytes)", bytes );
    CHECK_CALL( cuMemAlloc(&p, bytes), msg );
    fprintf( stderr, "   returned %p\n", (void*)p );
    return p;
}

void __dev_malloc_if_missing(buffer_t* buf) {
    if (buf->dev) return;
    fprintf(stderr, "dev_malloc_if_missing of %zux%zux%zux%zu (%zu bytes) buffer\n",
            buf->dims[0], buf->dims[1], buf->dims[2], buf->dims[3], buf->elem_size);
    size_t size = buf->dims[0] * buf->dims[1] * buf->dims[2] * buf->dims[3] * buf->elem_size;
    buf->dev = __dev_malloc(size);
}

void __copy_to_dev(buffer_t* buf) {
    size_t size = buf->dims[0] * buf->dims[1] * buf->dims[2] * buf->dims[3] * buf->elem_size;
    char msg[256];
    snprintf(msg, 256, "copy_to_dev (%zu bytes) %p -> %p", size, buf->host, (void*)buf->dev );
    CHECK_CALL( cuMemcpyHtoD(buf->dev, buf->host, size), msg );
}

void __copy_to_host(buffer_t* buf) {
    size_t size = buf->dims[0] * buf->dims[1] * buf->dims[2] * buf->dims[3] * buf->elem_size;
    char msg[256];
    snprintf(msg, 256, "copy_to_host (%zu bytes) %p -> %p", size, (void*)buf->dev, buf->host );
    CHECK_CALL( cuMemcpyDtoH(buf->host, buf->dev, size), msg );
}

void __dev_run(
    const char* entry_name,
    int blocksX, int blocksY, int blocksZ,
    int threadsX, int threadsY, int threadsZ,
    int shared_mem_bytes,
    void* args[])
{
    CUfunction f = __get_kernel(entry_name);
    char msg[256];
    snprintf(
        msg, 256,
        "dev_run %s with (%dx%dx%d) blks, (%dx%dx%d) threads",
        entry_name, blocksX, blocksY, blocksZ, threadsX, threadsY, threadsZ
    );
    CHECK_CALL(
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

#ifdef INCLUDE_WRAPPER
const char* ptx_src = "\n\
	.version 2.0\n\
	.target sm_11, map_f64_to_f32\n\
    \n\
.entry kernel (.param .b32 __param_1, .param .b64 __param_2) // @kernel\n\
{\n\
	.reg .b32 %r<6>;\n\
	.reg .b64 %rd<4>;\n\
// BB#0:                                // %entry\n\
	ld.param.u64	%rd0, [__param_2];\n\
	mov.u32	%r5, %ctaid.x;\n\
	shl.b32	%r1, %r5, 8;\n\
	mov.u32	%r2, %tid.x;\n\
	add.u32	%r3, %r1, %r2;\n\
	cvt.s64.s32	%rd1, %r3;\n\
	shl.b64	%rd2, %rd1, 2;\n\
	add.u64	%rd3, %rd0, %rd2;\n\
	mov.u32	%r4, 1067316150;\n\
	st.global.u32	[%rd3], %r4;\n\
	exit;\n\
}";
int f( buffer_t *input, buffer_t *result, int N )
{
    const char* entry_name = "kernel";
    __init(ptx_src);

    int threadsX = 256;
    int threadsY =  1;
    int threadsZ =  1;
    int blocksX = N / threadsX;
    int blocksY = 1;
    int blocksZ = 1;

    // __dev_malloc_if_missing(input);
    __dev_malloc_if_missing(result);

    // __copy_to_dev(input);

    // Invoke
    void* cuArgs[] = { &N, &result->dev };
    __dev_run(
        entry_name,
        blocksX,  blocksY,  blocksZ,
        threadsX, threadsY, threadsZ,
        0, // sharedMemBytes
        cuArgs
    );

    // Sync and copy back
    // CHECK_CALL( cuCtxSynchronize(), "pre-sync" ); // only necessary for async copies?
    __copy_to_host(result);
    // CHECK_CALL( cuCtxSynchronize(), "post-sync" );
    
    return 0;
}

#endif

} // extern "C" linkage
