#include <stdio.h>

#ifdef _WIN32
// This test requires weak linkage
int main(int argc, char **argv) {
  printf("Skipping test on windows\n");
  return 0;
}
#else

#include "HalideRuntime.h"
#include "HalideBuffer.h"
#include "HalideRuntimeOpenCL.h"
#include <assert.h>
#include <string.h>

#include "define_extern_opencl.h"

using namespace Halide::Runtime;

const int W = 256;

#if defined(TEST_OPENCL)
// Implement OpenCL custom context.

#define CL_USE_DEPRECATED_OPENCL_1_2_APIS
#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

// Just use a global context and queue, created and destroyed by main.
cl_context cl_ctx = nullptr;
cl_command_queue cl_q = nullptr;
cl_device_id cl_dev;

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

    cl_dev = devices[deviceCount - 1];

    // Create context and command queue.
    cl_context_properties properties[] = { CL_CONTEXT_PLATFORM, (cl_context_properties)platform,
                                           0 };
    cl_ctx = clCreateContext(properties, 1, &cl_dev, nullptr, nullptr, &err);
    if (err != CL_SUCCESS) {
        printf("clCreateContext failed (%d)\n", err);
        return err;
    }

    cl_q = clCreateCommandQueue(cl_ctx, cl_dev, 0, &err);
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
    *ctx = cl_ctx;
    *q = cl_q;
    return 0;
}

extern "C" int halide_release_cl_context(void *user_context) {
    return 0;
}

extern "C" int32_t gpu_input(halide_buffer_t *input, halide_buffer_t *output) {
    if (input->host == nullptr) {
        printf("gpu_input: Bounds query for output size %d\n", output->dim[0].extent);
        input->type = output->type;
        input->dimensions = 1;
        input->dim[0] = output->dim[0];
        return 0;
    }

    printf("gpu_input: Called to compute on size %d\n", input->dim[0].extent);
    assert(output->device != 0);

    halide_copy_to_device(nullptr, input, halide_opencl_device_interface());

    cl_int error;
    const char *ocl_program = "__kernel void add42(__global const int *in, __global int *out) { out[get_global_id(0)] = in[get_global_id(0)] + 42; }";
    const char *sources[] = { ocl_program };
    cl_program program = clCreateProgramWithSource(cl_ctx, 1, &sources[0], NULL, &error);
    assert(error == CL_SUCCESS);
    cl_device_id devices[] = { cl_dev };
    error = clBuildProgram(program, 1, devices, NULL, NULL, NULL);
    if (error == CL_BUILD_PROGRAM_FAILURE) {
        size_t msg_size;
        clGetProgramBuildInfo(program, cl_dev, CL_PROGRAM_BUILD_LOG, 0, NULL, &msg_size);
        char *msg = (char *)malloc(msg_size);
        clGetProgramBuildInfo(program, cl_dev, CL_PROGRAM_BUILD_LOG, msg_size, msg, NULL);
        printf("Error message %s\n", msg);
    }
    assert(error == CL_SUCCESS);

    cl_kernel kernel = clCreateKernel(program, "add42", &error);
    assert(error == CL_SUCCESS);
    size_t global_dim[1] = { (size_t)input->dim[0].extent };
    size_t local_dim[1] = { 16 };

    // Set args
    void *in = reinterpret_cast<void *>(input->device);
    error = clSetKernelArg(kernel, 0, sizeof(void *), &in);
    assert(error == CL_SUCCESS);
    void *out = reinterpret_cast<void *>(output->device);
    error = clSetKernelArg(kernel, 1, sizeof(void *), &out);
    assert(error == CL_SUCCESS);

    printf("gpu_input: Calling clEnqueueNDRangeKernel.\n");
    error = clEnqueueNDRangeKernel(cl_q, kernel, 1, nullptr, global_dim, local_dim, 0, nullptr, nullptr);
    printf("gpu_input: Returned from clEnqueueNDRangeKernel with result %d.\n", error);
    assert(error == CL_SUCCESS);

    return 0;
}

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
    Buffer<int32_t> input(W);
    for (int x = 0; x < W; x++) {
        input(x) = x;
    }
    input.set_host_dirty(true);

    Buffer<int32_t> output(W);

    define_extern_opencl(input, output);
    output.copy_to_host();

    for (int x = 0; x < W; x++) {
        if (input(x) + 1 != output(x)) {
             printf("Error at (%d): %d != %d\n", x, input(x) + 1, output(x));
             return -1;
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
