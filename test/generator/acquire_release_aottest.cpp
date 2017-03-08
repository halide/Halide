#include <stdio.h>

#ifdef _WIN32
// This test requires weak linkage
int main(int argc, char **argv) {
  printf("Skipping test on windows\n");
  return 0;
}
#else

#include <math.h>
#include "HalideRuntime.h"
#include "HalideBuffer.h"
#include <assert.h>
#include <string.h>

#include "acquire_release.h"


using namespace Halide::Runtime;

const int W = 256, H = 256;

#if defined(TEST_OPENCL)
// Implement OpenCL custom context.

#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

// Just use a global context and queue, created and destroyed by main.
cl_context cl_ctx = nullptr;
cl_command_queue cl_q = nullptr;

// Create the global context. This is just a helper function not called by Halide.
int init_context() {
    cl_int err = 0;

    const cl_uint maxPlatforms = 4;
    cl_platform_id platforms[maxPlatforms];
    cl_uint platformCount = 0;

    err = clGetPlatformIDs(maxPlatforms, platforms, &platformCount);
    if (err != CL_SUCCESS) {
        printf("clGetPlatformIDs failed (%d)\n", err);
        return err;
    }

    cl_platform_id platform = nullptr;

    if (platformCount > 0) {
        platform = platforms[0];
    }
    if (platform == nullptr) {
        printf("Failed to get platform\n");
        return CL_INVALID_PLATFORM;
    }

    cl_device_type device_type = CL_DEVICE_TYPE_ALL;

    // Make sure we have a device
    const cl_uint maxDevices = 4;
    cl_device_id devices[maxDevices];
    cl_uint deviceCount = 0;
    err = clGetDeviceIDs(platform, device_type, maxDevices, devices, &deviceCount);
    if (err != CL_SUCCESS) {
        printf("clGetDeviceIDs failed (%d)\n", err);
        return err;
    }
    if (deviceCount == 0) {
        printf("Failed to get device\n");
        return CL_DEVICE_NOT_FOUND;
    }

    cl_device_id dev = devices[deviceCount - 1];

    // Create context and command queue.
    cl_context_properties properties[] = { CL_CONTEXT_PLATFORM, (cl_context_properties)platform,
                                           0 };
    cl_ctx = clCreateContext(properties, 1, &dev, nullptr, nullptr, &err);
    if (err != CL_SUCCESS) {
        printf("clCreateContext failed (%d)\n", err);
        return err;
    }

    cl_q = clCreateCommandQueue(cl_ctx, dev, 0, &err);
    if (err != CL_SUCCESS) {
        printf("clCreateCommandQueue failed (%d)\n", err);
        return err;
    }
    printf("Created CL context %p\n", cl_ctx);
    return 0;
}

void destroy_context() {
    printf("Destroying CL context %p\n", cl_ctx);
    clReleaseCommandQueue(cl_q);
    clReleaseContext(cl_ctx);
    cl_q = nullptr;
    cl_ctx = nullptr;
}

// These functions replace the acquire/release implementation in src/runtime/opencl.cpp.
// Since we don't parallelize access to the GPU in the schedule, we don't need synchronization
// in our implementation of these functions.
extern "C" int halide_acquire_cl_context(void *user_context, cl_context *ctx, cl_command_queue *q) {
    printf("Acquired CL context %p\n", cl_ctx);
    *ctx = cl_ctx;
    *q = cl_q;
    return 0;
}

extern "C" int halide_release_cl_context(void *user_context) {
    printf("Releasing CL context %p\n", cl_ctx);
    return 0;
}
#elif defined(TEST_CUDA)
// Implement CUDA custom context.
#include <cuda.h>

CUcontext cuda_ctx = nullptr;

int init_context() {
    // Initialize CUDA
    CUresult err = cuInit(0);
    if (err != CUDA_SUCCESS) {
        printf("cuInit failed (%d)\n", err);
        return err;
    }

    // Make sure we have a device
    int deviceCount = 0;
    err = cuDeviceGetCount(&deviceCount);
    if (err != CUDA_SUCCESS) {
        printf("cuGetDeviceCount failed (%d)\n", err);
        return err;
    }
    if (deviceCount <= 0) {
        printf("No CUDA devices available\n");
        return CUDA_ERROR_NO_DEVICE;
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
        return err;
    }
    printf("Created CUDA context %p\n", cuda_ctx);

    return 0;
}

void destroy_context() {
    printf("Destroying CUDA context %p\n", cuda_ctx);
    cuCtxDestroy(cuda_ctx);
    cuda_ctx = nullptr;
}

// These functions replace the acquire/release implementation in src/runtime/cuda.cpp.
// Since we don't parallelize access to the GPU in the schedule, we don't need synchronization
// in our implementation of these functions.
extern "C" int halide_acquire_cuda_context(void *user_context, CUcontext *ctx) {
    printf("Acquired CUDA context %p\n", cuda_ctx);
    *ctx = cuda_ctx;
    return 0;
}

extern "C" int halide_release_cuda_context(void *user_context) {
    printf("Releasing CUDA context %p\n", cuda_ctx);
    return 0;
}
#else
// Just use the default implementation of acquire/release.
int init_context() {
    printf("Using default implementation of acquire/release\n");
    return 0;
}
void destroy_context() {}
#endif

int main(int argc, char **argv) {
    // Initialize the runtime specific GPU context.
    int ret = init_context();
    if (ret != 0) {
        return ret;
    }

    // Everything else is a normal Halide program. The GPU runtime will call
    // the above acquire/release functions to get the context instead of using
    // its own internal context.
    Buffer<float> input(W, H);
    for (int y = 0; y < input.height(); y++) {
        for (int x = 0; x < input.width(); x++) {
            input(x, y) = (float)(x * y);
        }
    }

    input.set_host_dirty(true);

    Buffer<float> output(W, H);

    acquire_release(input, output);

    output.copy_to_host();

    for (int y = 0; y < output.height(); y++) {
        for (int x = 0; x < output.width(); x++) {
            if (input(x, y) * 2.0f + 1.0f != output(x, y)) {
                printf("Error at (%d, %d): %f != %f\n", x, y, input(x, y) * 2.0f + 1.0f,
                       output(x, y));
                return -1;
            }
        }
    }

    // We need to free our GPU buffers before destroying the context.
    input.device_free();
    output.device_free();

    // Free the context we created.
    destroy_context();

    printf("Success!\n");
    return 0;
}

#endif
