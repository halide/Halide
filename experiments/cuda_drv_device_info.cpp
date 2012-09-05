#include <stdio.h>
#include <assert.h>
#include <math.h>
#include <cuda.h>

#define CHECK_CALL(c,str) {\
    fprintf(stderr, "Do %s\n", str); \
    CUresult status = (c); \
    if (status != CUDA_SUCCESS) \
        fprintf(stderr, "CUDA: %s returned non-success: %d\n", str, status); \
    assert(status == CUDA_SUCCESS); \
} sqrt(0.0) // just *some* expression fragment after which it's legal to put a ;

int main()
{
    CHECK_CALL( cuInit(0), "cuInit" );
    
    // Number of CUDA devices
    int devCount;
    CHECK_CALL( cuDeviceGetCount(&devCount), "cuDeviceGetCount" );
    printf("CUDA Device Query...\n");
    printf("There are %d CUDA devices.\n", devCount);
 
    // Iterate through devices
    for (int i = 0; i < devCount; ++i)
    {
        // Get device properties
        printf("\nCUDA Device #%d\n", i);
        
        CUdevice dev;
        CUcontext ctx;
        CHECK_CALL( cuDeviceGet(&dev, i), "cuDeviceGet" );
        CHECK_CALL( cuCtxCreate(&ctx, 0, dev), "cuCtxCreate" );
        //CHECK_CALL( cuCtxPushCurrent(ctx), "cuCtxPushCurrent" );
        
        // cudaDeviceProp devProp;
        // cudaGetDeviceProperties(&devProp, i);
        // printDevProp(devProp);
        
        
        size_t f, t;
        CHECK_CALL( cuMemGetInfo(&f, &t), "cuMemGetInfo" );
        printf("\n%zu free, %zu total memory\n", f, t);
        
        // printLimits();
    }
 
    printf("\nPress any key to exit...");
    char c;
    scanf("%c", &c);
 
    return 0;
}
