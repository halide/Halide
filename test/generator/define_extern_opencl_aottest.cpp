#include "HalideRuntime.h"
#include <stdio.h>

#ifdef _WIN32

// Avoid link errors
extern "C" int32_t gpu_input(halide_buffer_t *input, halide_buffer_t *output) {
    return 0;
}

int main(int argc, char **argv) {
    // TODO: is this true?
    printf("[SKIP] OpenCL headers/libs are not properly setup yet for Windows.\n");
    return 0;
}

#elif !defined(TEST_OPENCL)

// Avoid link errors
extern "C" int32_t gpu_input(halide_buffer_t *input, halide_buffer_t *output) {
    return 0;
}

int main(int argc, char **argv) {
    printf("[SKIP] Test requires OpenCL.\n");
    return 0;
}

#else

#include "HalideBuffer.h"
#include "HalideRuntimeOpenCL.h"
#include <assert.h>
#include <string.h>

#include "define_extern_opencl.h"

using namespace Halide::Runtime;

const int W = 256;

// Implement OpenCL custom context.

#define CL_USE_DEPRECATED_OPENCL_1_2_APIS
#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

// TODO: Figure out how to get these declared in HalideRuntimeOpenCL.h.
// Probably just require that the OpenCL headers are included before
// including that header. Or optionally have the header declare the types.
// (Issue applies to all device APIs of course.)
extern "C" int halide_acquire_cl_context(void *user_context, cl_context *ctx, cl_command_queue *q, bool create = true);
extern "C" int halide_release_cl_context(void *user_context);

namespace {

cl_program ocl_program;

cl_int init_extern_program() {
    cl_int error;

    cl_context ocl_ctx = nullptr;
    cl_command_queue ocl_q = nullptr;
    int halide_error = halide_acquire_cl_context(nullptr, &ocl_ctx, &ocl_q);
    if (halide_error != 0) {
        printf("halide_acquire_cl_context failed (%d).\n", halide_error);
        return halide_error;
    }

    const char *ocl_source = "__kernel void add42(__global const int *in, __global int *out) { out[get_global_id(0)] = in[get_global_id(0)] + 42; }";
    const char *sources[] = {ocl_source};
    ocl_program = clCreateProgramWithSource(ocl_ctx, 1, &sources[0], nullptr, &error);
    if (error != CL_SUCCESS) {
        halide_release_cl_context(nullptr);
        printf("clCreateProgramWithSource failed (%d).\n", error);
        return error;
    }

    const cl_uint maxDevices = 4;
    cl_device_id devices[maxDevices];
    size_t actual_size = 0;
    error = clGetContextInfo(ocl_ctx, CL_CONTEXT_DEVICES, sizeof(devices), devices, &actual_size);
    if (error != CL_SUCCESS) {
        printf("clGetContextInfo failed (%d).\n", error);
        halide_release_cl_context(nullptr);
        return error;
    }

    error = clBuildProgram(ocl_program, actual_size / sizeof(devices[0]), devices, nullptr, nullptr, nullptr);

    halide_release_cl_context(nullptr);

    if (error != CL_SUCCESS) {
        if (error == CL_BUILD_PROGRAM_FAILURE) {
            size_t msg_size;
            clGetProgramBuildInfo(ocl_program, devices[0], CL_PROGRAM_BUILD_LOG, 0, nullptr, &msg_size);
            char *msg = (char *)malloc(msg_size);
            clGetProgramBuildInfo(ocl_program, devices[0], CL_PROGRAM_BUILD_LOG, msg_size, msg, nullptr);
            printf("clBuildProgram failed. Error message %s\n", msg);
        } else {
            printf("clBuildProgram failed (%d).\n", error);
        }
        clReleaseProgram(ocl_program);
        return error;
    }

    return error;
}

void destroy_extern_program() {
    clReleaseProgram(ocl_program);
}

}  // namespace

extern "C" int32_t gpu_input(halide_buffer_t *input, halide_buffer_t *output) {
    if (input->is_bounds_query()) {
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
    cl_context ocl_ctx = nullptr;
    cl_command_queue ocl_q = nullptr;
    int halide_result = halide_acquire_cl_context(nullptr, &ocl_ctx, &ocl_q);
    assert(halide_result == 0);

    cl_kernel kernel = clCreateKernel(ocl_program, "add42", &error);
    if (error != CL_SUCCESS) {
        printf("clCreateKernel failed (%d).\n", error);
    }

    size_t global_dim[1] = {(size_t)input->dim[0].extent};
    size_t local_dim[1] = {16};

    // Set args
    void *in = reinterpret_cast<void *>(halide_opencl_get_cl_mem(nullptr, input));
    error = clSetKernelArg(kernel, 0, sizeof(void *), &in);
    assert(error == CL_SUCCESS);
    void *out = reinterpret_cast<void *>(halide_opencl_get_cl_mem(nullptr, output));
    error = clSetKernelArg(kernel, 1, sizeof(void *), &out);
    assert(error == CL_SUCCESS);

    printf("gpu_input: Calling clEnqueueNDRangeKernel.\n");
    error = clEnqueueNDRangeKernel(ocl_q, kernel, 1, nullptr, global_dim, local_dim, 0, nullptr, nullptr);
    printf("gpu_input: Returned from clEnqueueNDRangeKernel with result %d.\n", error);
    assert(error == CL_SUCCESS);

    clReleaseKernel(kernel);

    halide_release_cl_context(nullptr);

    // Return with the kernel queued. Halide guarantees any use of the
    // device buffer will happen on the same queue or there will be a
    // sync on the queue first.

    return 0;
}

int main(int argc, char **argv) {
    {
        // Make sure the OpenCL library is loaded/symbols looked up in Halide
        Buffer<int32_t, 1> buf(32);
        buf.device_malloc(halide_opencl_device_interface());
    }

    // Initialize a small OpenCL program to test extern calls.
    int ret = init_extern_program();
    if (ret != 0) {
        return ret;
    }

    // Everything else is a normal Halide program. The GPU runtime will call
    // the above acquire/release functions to get the context instead of using
    // its own internal context.
    Buffer<int32_t, 1> input(W);
    for (int x = 0; x < W; x++) {
        input(x) = x;
    }
    input.set_host_dirty(true);

    Buffer<int32_t, 1> output(W);

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

    // Free the program and kernel resources.
    destroy_extern_program();
    // Free the context we created.
    //    destroy_context();

    printf("Success!\n");
    return 0;
}

#endif
