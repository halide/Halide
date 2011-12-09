// Test CUDA kernel runner, using the low-level Driver API

/*
 * Build LL module with:
 *
 *   clang -I/usr/local/cuda/include -c -S -emit-llvm ptx_shim_tmpl.c -o ptx_shim_tmpl.ll
 */
#include <stdio.h>
//#include <cuda.h>
//#include <assert.h>

//#define CHECK_CALL(c) (assert((c) == CUDA_SUCCESS))
//#define CHECK_CALL(c) (success &&= ((c) == CUDA_SUCCESS))
#define CHECK_CALL(c) (c)

/*
typedef union {
    struct {
        void* host;
        CUdeviceptr dev;
    } ptr;
    int32_t i32;
    float f32;
} ArgT;
*/

// TODO: convention to track host and device pointers together in args

// TODO: use tags to track arg types for simple, non-dynamically-generated free function? Or just generate free function like allocate/run function, with explicit knowledge of which arguments are which and how to free them?

// Convention: caller passes in args array as would be passed to function, but 
// with ptr args unset. Buffers are allocated and pointers assigned to ptr args.
// Useful to automatically preallocate buffers in generated code, while allowing 
// them to be reused through multiple applications of the same entrypoint.
/*
 * Punting on this for now
 *
int allocateBuffers(ArgT* args)
{
    int width = args[2].i32;
    int height = args[3].i32;
    int channels = args[4].i32;

    size_t inSize = width*height*channels*sizeof(float);
    args[0].host = malloc(inSize);
    cuMemAlloc(args[0].device, inSize);

    size_t inSize = width*height*channels*sizeof(float);
    args[1].host = malloc(outSize);
    cuMemAlloc(args[1].device, outSize);
}
*/

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
typedef enum { CUDA_SUCCESS = 0, CUDA_ANY_ERROR = 1 } CUresult; // stub for real enum

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
char* ptx_src_ptr;

/*
typedef enum bool {
    false = 0,
    true = 1
} bool;
*/
void init()
{
    /*bool success;*/
    if (!f) {
        // Initialize CUDA
        CHECK_CALL( cuInit(0) );

        // Make sure we have a device
        int deviceCount = 0;
        CHECK_CALL( cuDeviceGetCount(&deviceCount) );
        /*if (deviceCount == 0) return false;*/

        // Get device
        CHECK_CALL( cuDeviceGet(&dev, 0) );

        // Create context
        CHECK_CALL( cuCtxCreate(&ctx, 0, dev) );

        // Create module
        CHECK_CALL( cuModuleLoadData(&mod, ptx_src_ptr) );

        // Get kernel function ptr
        //assert(!f);
        CHECK_CALL( cuModuleGetFunction(&f, mod, "_im_main") );

        /*if (!success) return false;*/
    }
    /*return true;*/
}

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
    cuMemAlloc(&p, bytes);
    return p;
}

void dev_malloc_if_missing(buffer_t* buf) {
    if (buf->dev) return;
    size_t size = buf->dims[0] * buf->dims[1] * buf->dims[2] * buf->dims[3] * buf->elem_size;
    buf->dev = dev_malloc(size);
}

void copy_to_dev(buffer_t* buf) {
    size_t size = buf->dims[0] * buf->dims[1] * buf->dims[2] * buf->dims[3] * buf->elem_size;
    cuMemcpyHtoD(buf->dev, buf->host, size);
}

void copy_to_host(buffer_t* buf) {
    size_t size = buf->dims[0] * buf->dims[1] * buf->dims[2] * buf->dims[3] * buf->elem_size;
    cuMemcpyDtoH(buf->host, buf->dev, size);
}

void dev_run(
    int blocksX, int blocksY, int blocksZ,
    int threadsX, int threadsY, int threadsZ,
    int shared_mem_bytes,
    void* args[])
{
    cuLaunchKernel(
        f,
        blocksX,  blocksY,  blocksZ,
        threadsX, threadsY, threadsZ,
        shared_mem_bytes,
        NULL, // stream
        args,
        NULL
    );
}

#ifdef INCLUDE_WRAPPER
int kernel_wrapper_tmpl( buffer_t *input, buffer_t *result, int N )
{
    /*bool success;*/

    /*if (!init()) return false;*/
    init();

    // Copy host data to device
    // NOTE: this is the caller responsibility for now
    //CHECK_CALL( cuMemcpyHtoD(dIn, hIn, size) );

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
