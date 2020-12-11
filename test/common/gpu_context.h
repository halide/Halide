#if defined(TEST_OPENCL)
// Implement OpenCL custom context.

#define CL_TARGET_OPENCL_VERSION 120
#define CL_USE_DEPRECATED_OPENCL_1_2_APIS
#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

// Just use a global context and queue, created and destroyed by main.
cl_context cl_ctx = nullptr;
cl_command_queue cl_q = nullptr;

// Create the global context. This is just a helper function not called by Halide.
bool create_opencl_context(cl_context &cl_ctx, cl_command_queue &cl_q) {
    cl_int err = 0;

    const cl_uint maxPlatforms = 4;
    cl_platform_id platforms[maxPlatforms];
    cl_uint platformCount = 0;

    err = clGetPlatformIDs(maxPlatforms, platforms, &platformCount);
    if (err != CL_SUCCESS) {
        printf("clGetPlatformIDs failed (%d)\n", err);
        return false;
    }

    cl_platform_id platform = nullptr;

    if (platformCount > 0) {
        platform = platforms[0];
    }
    if (platform == nullptr) {
        printf("Failed to get platform\n");
        return false;
    }

    cl_device_type device_type = CL_DEVICE_TYPE_ALL;

    // Make sure we have a device
    const cl_uint maxDevices = 4;
    cl_device_id devices[maxDevices];
    cl_uint deviceCount = 0;
    err = clGetDeviceIDs(platform, device_type, maxDevices, devices, &deviceCount);
    if (err != CL_SUCCESS) {
        printf("clGetDeviceIDs failed (%d)\n", err);
        return false;
    }
    if (deviceCount == 0) {
        printf("Failed to get device\n");
        return false;
    }

    cl_device_id dev = devices[deviceCount - 1];

    // Create context and command queue.
    cl_context_properties properties[] = {CL_CONTEXT_PLATFORM, (cl_context_properties)platform,
                                          0};
    cl_ctx = clCreateContext(properties, 1, &dev, nullptr, nullptr, &err);
    if (err != CL_SUCCESS) {
        printf("clCreateContext failed (%d)\n", err);
        return false;
    }

    cl_q = clCreateCommandQueue(cl_ctx, dev, 0, &err);
    if (err != CL_SUCCESS) {
        printf("clCreateCommandQueue failed (%d)\n", err);
        return false;
    }
    return true;
}

void destroy_opencl_context(cl_context cl_ctx, cl_command_queue cl_q) {
    clReleaseCommandQueue(cl_q);
    clReleaseContext(cl_ctx);
}

#elif defined(TEST_CUDA)
// Implement CUDA custom context.
#include <cuda.h>

bool create_cuda_context(CUcontext &cuda_ctx) {
    // Initialize CUDA
    CUresult err = cuInit(0);
    if (err != CUDA_SUCCESS) {
        printf("cuInit failed (%d)\n", err);
        return false;
    }

    // Make sure we have a device
    int deviceCount = 0;
    err = cuDeviceGetCount(&deviceCount);
    if (err != CUDA_SUCCESS) {
        printf("cuGetDeviceCount failed (%d)\n", err);
        return false;
    }
    if (deviceCount <= 0) {
        printf("No CUDA devices available\n");
        return false;
    }

    CUdevice dev;
    // Get device
    CUresult status;
    // Try to get a device >0 first, since 0 should be our display device
    // For now, don't try devices > 2 to maintain compatibility with previous behavior.
    if (deviceCount > 2) deviceCount = 2;
    for (int id = deviceCount - 1; id >= 0; id--) {
        status = cuDeviceGet(&dev, id);
        if (status == CUDA_SUCCESS) break;
    }

    if (status != CUDA_SUCCESS) {
        printf("Failed to get CUDA device\n");
        return status;
    }

    // Create context
    err = cuCtxCreate(&cuda_ctx, 0, dev);
    if (err != CUDA_SUCCESS) {
        printf("cuCtxCreate failed (%d)\n", err);
        return false;
    }

    return true;
}

void destroy_cuda_context(CUcontext cuda_ctx) {
    cuCtxDestroy(cuda_ctx);
}

#elif defined(TEST_METAL) && defined(__OBJC__)
#include <Metal/MTLCommandQueue.h>
#include <Metal/MTLDevice.h>

void create_metal_context(id<MTLDevice> &device, id<MTLCommandQueue> &queue) {
    device = MTLCreateSystemDefaultDevice();
    if (device == nullptr) {
        NSArray<id<MTLDevice>> *devices = MTLCopyAllDevices();
        if (devices != nullptr) {
            device = devices[0];
        }
    }
    assert(device != nullptr);
    queue = [device newCommandQueue];
    assert(queue != nullptr);
}  

void destroy_metal_context(id<MTLDevice> device, id<MTLCommandQueue> queue) {
    [queue release];
    [device release];
}

#endif
