/*
 * Build LL module with:
 *
 *   clang -I/usr/local/cuda/include -c -S -emit-llvm ptx_shim_tmpl.c -o ptx_shim_tmpl.ll
 */
#include <stdio.h>
//#include <cuda.h>
#include <assert.h>

//#define CHECK_CALL(c) (assert((c) == CUDA_SUCCESS))
//#define CHECK_CALL(c) (success &&= ((c) == CUDA_SUCCESS))
/*#define CHECK_CALL(c) (c)*/
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
#define cuMemcpyHtoD                        cuMemcpyHtoD_v2
#define cuMemcpyDtoH                        cuMemcpyDtoH_v2

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
#endif //__cuda_cuda_h__

// Device, Context, Module, and Function for this entrypoint are tracked locally
// and constructed lazily on first run.
static CUfunction f = 0;
static CUdevice dev = 0;
static CUcontext ctx = 0;
static CUmodule mod = 0;

void init(const char* ptx_src, const char* entry_name)
{
    CUresult status;

    if (!f) {
        // Initialize CUDA
        CHECK_CALL( cuInit(0), "cuInit" );

        // Make sure we have a device
        int deviceCount = 0;
        CHECK_CALL( cuDeviceGetCount(&deviceCount), "cuDeviceGetCount" );
        assert(deviceCount > 0);

        // Get device
        CHECK_CALL( cuDeviceGet(&dev, 0), "cuDeviceGet" );

        // Create context
        CHECK_CALL( cuCtxCreate(&ctx, 0, dev), "cuCtxCreate" );

        // Create module
        CHECK_CALL( cuModuleLoadData(&mod, ptx_src), "cuModuleLoadData" );
        
        fprintf(stderr, "-------\nCompiling PTX:\n%s\n--------\n", ptx_src);

        // Get kernel function ptr
        CHECK_CALL( cuModuleGetFunction(&f, mod, entry_name), "cuModuleGetFunction" );
    }
}

// TODO: switch to building as C++ with extern "C" linkage on functions
typedef enum {
    false = 0,
    true = 1
} Bool;

typedef struct {
    char* host;
    CUdeviceptr dev;
    Bool host_dirty;
    Bool dev_dirty;
    size_t dims[4];
    size_t elem_size;
} buffer_t;

CUdeviceptr dev_malloc(size_t bytes) {
    CUdeviceptr p;
    CHECK_CALL( cuMemAlloc(&p, bytes), "dev_malloc" );
    return p;
}

void dev_malloc_if_missing(buffer_t* buf) {
    if (buf->dev) return;
    size_t size = buf->dims[0] * buf->dims[1] * buf->dims[2] * buf->dims[3] * buf->elem_size;
    buf->dev = dev_malloc(size);
}

void copy_to_dev(buffer_t* buf) {
    size_t size = buf->dims[0] * buf->dims[1] * buf->dims[2] * buf->dims[3] * buf->elem_size;
    CHECK_CALL( cuMemcpyHtoD(buf->dev, buf->host, size), "copy_to_dev" );
}

void copy_to_host(buffer_t* buf) {
    size_t size = buf->dims[0] * buf->dims[1] * buf->dims[2] * buf->dims[3] * buf->elem_size;
    CHECK_CALL( cuMemcpyDtoH(buf->host, buf->dev, size), "copy_to_host" );
}

void dev_run(
    int blocksX, int blocksY, int blocksZ,
    int threadsX, int threadsY, int threadsZ,
    int shared_mem_bytes,
    void* args[])
{
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
        "cuLaunchKernel"
    );
}

#ifdef INCLUDE_WRAPPER
int kernel_wrapper_tmpl( buffer_t *input, buffer_t *result, int N )
{
    const char* ptx_src = "...";
    const char* entry_name = "f";
    init(ptx_src, entry_name);

    int threadsX = 16;
    int threadsY =  1;
    int threadsZ =  1;
    int blocksX = (N + threadsX - 1 ) / threadsX;
    int blocksY = 1;
    int blocksZ = 1;

    dev_malloc_if_missing(input);
    dev_malloc_if_missing(result);

    copy_to_dev(input);

    // Invoke
    void* cuArgs[] = { &input->dev, &result->dev, &N };
    dev_run(
        blocksX,  blocksY,  blocksZ,
        threadsX, threadsY, threadsZ,
        0, // sharedMemBytes
        cuArgs
    );
    
    // Sync and copy back
    //CHECK_CALL( cuCtxSynchronize() ); // only necessary for async copies?
    copy_to_host(result);
    //CHECK_CALL( cuCtxSynchronize() );
    
    return 0;
}
#endif
