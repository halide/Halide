// Test CUDA kernel runner, using the low-level Driver API

/*
 * Build with:
 *
 *   g++ -I/usr/local/cuda/include -L/usr/local/cuda/lib -lcuda test_hello_ptx.cpp
 *
 * Requires test_hello_ptx.ptx to be in the same folder at runtime.
 */
#include <stdio.h>
#include <string.h>
#include <cuda.h>
#include <assert.h>

#define CHECK_CALL(c) (assert((c) == CUDA_SUCCESS))

int N = 1024*1024;
size_t size = N * sizeof(float);
int threadsPerBlock = 256;
int blocksPerGrid = (N + threadsPerBlock - 1) / threadsPerBlock;

void printSample(float* arr, char* name)
{
    for(size_t i = 0; i < N; i += 10000) {
        printf("%s(%lld): %f\n", name, i, arr[i]);
    }
}

int main(int argc, char const *argv[])
{
    CUresult err;

    // Allocate host vectors
    float* hIn = (float*)malloc(size);
    float* hOut = (float*)malloc(size);
    
    // Initialize input
    // TODO make this an argument
    for(size_t i = 0; i < N; ++i)
        hIn[i] = float(i);
    
    printSample(hOut, "out before");
    printSample(hIn, "in");
    
    // Initialize CUDA
    CHECK_CALL( cuInit(0) );
    
    // Make sure we have a device
    int deviceCount = 0;
    CHECK_CALL( cuDeviceGetCount(&deviceCount) );
    assert(deviceCount);
    
    // Get device
    CUdevice dev;
    CHECK_CALL( cuDeviceGet(&dev, 0) );
    
    // Create context
    CUcontext ctx;
    CHECK_CALL( cuCtxCreate(&ctx, 0, dev) );
    
    // Create module
    CUmodule mod;
    CHECK_CALL( cuModuleLoad(&mod, "test_hello_ptx.ptx") );
    
    // Alloc device vectors
    CUdeviceptr dIn, dOut;
    CHECK_CALL( cuMemAlloc(&dIn, size) );
    CHECK_CALL( cuMemAlloc(&dOut, size) );
    
    // Copy host data to device
    CHECK_CALL( cuMemcpyHtoD(dIn, hIn, size) );
    
    // Get kernel function ptr
    CUfunction k;
    CHECK_CALL( cuModuleGetFunction(&k, mod, "_im_main") ); // TODO: make this an argument
    
    // Invoke
    void* args[] = { &dIn, &dOut, &N };
    CHECK_CALL(
        cuLaunchKernel(
            k,
            blocksPerGrid, 1, 1,
            threadsPerBlock, 1, 1,
            0,
            NULL, args, NULL
        )
    );
    
    // Sync and copy back
    //CHECK_CALL( cuCtxSynchronize() ); // only necessary for async copies?
    CHECK_CALL( cuMemcpyDtoH(hOut, dOut, size) );
    //CHECK_CALL( cuCtxSynchronize() );
    
    printSample(hOut, "out after");
    
    return 0;
}
