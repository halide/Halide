// Test CUDA kernel runner, using the low-level Driver API

/*
 * Build with:
 *
 *   g++ -I/usr/local/cuda/include -L/usr/local/cuda/lib -lcuda test_hello_ptx.cpp
 *
 * Requires test_hello_ptx.ptx to be in the same folder at runtime.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
//#include <cuda.h>
#include <assert.h>

#define CHECK_CALL(c) (assert((c) == CUDA_SUCCESS))

int threads = 256;
int blocks = 64;
int N = threads*blocks;
size_t size = N * sizeof(int);

#if 0
#ifndef __cuda_cuda_h__
#if defined(__x86_64) || defined(AMD64) || defined(_M_AMD64)
typedef unsigned long long CUdeviceptr;
#else
typedef unsigned int CUdeviceptr;
#endif
#endif
#endif

#include "buffer.h"

extern "C" void f(buffer_t *input, buffer_t *result, int N);

void printSample(int* arr, char* name)
{
    float* arrf = (float*)arr;
    for(size_t i = 0; i < N; i += 128) {
        printf("%s(%zu): 0x%x (%d) (%f)\n", name, i, arr[i], arr[i], arrf[i]);
    }
}

int main(int argc, char const *argv[])
{
    // Allocate host vectors
    int* hIn  = (int*)malloc(size);
    int* hOut = (int*)malloc(size);
    
    // Initialize input
    // TODO make this an argument
    for(size_t i = 0; i < N; ++i)
        hIn[i] = int(i);
    
    printSample(hOut, "out before");
    printSample(hIn, "in");
    
#if 1
    buffer_t in, out;
    in.host = (uint8_t*)hIn;
    in.dev = 0;
    in.dims[0] = N;
    in.dims[1] = in.dims[2] = in.dims[3] = 1;
    in.elem_size = sizeof(int);
    out.host = (uint8_t*)hOut;
    out.dev = 0;
    out.dims[0] = N;
    out.dims[1] = out.dims[2] = out.dims[3] = 1;
    out.elem_size = sizeof(int);

    f(&in, &out, N);
#else
    CUresult err;

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
#endif
    printSample(hOut, "out after");
    
    return 0;
}
