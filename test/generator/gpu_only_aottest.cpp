#include "HalideBuffer.h"
#include "HalideRuntime.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#if defined(TEST_OPENCL)
#include "HalideRuntimeOpenCL.h"
#elif defined(TEST_CUDA)
#include "HalideRuntimeCuda.h"
#elif defined(TEST_METAL)
#include "HalideRuntimeMetal.h"
#elif defined(TEST_WEBGPU)
#include "HalideRuntimeWebGPU.h"
#endif

#include "gpu_only.h"
using namespace Halide::Runtime;

#if defined(TEST_OPENCL)

#if !defined(HALIDE_RUNTIME_OPENCL)
#error "TEST_OPENCL defined but HALIDE_RUNTIME_OPENCL not defined"
#endif

#elif defined(TEST_CUDA)

#if !defined(HALIDE_RUNTIME_CUDA)
#error "TEST_CUDA defined but HALIDE_RUNTIME_CUDA not defined"
#endif

#elif defined(TEST_WEBGPU)

#if !defined(HALIDE_RUNTIME_WEBGPU)
#error "TEST_WEBGPU defined but HALIDE_RUNTIME_WEBGPU not defined"
#endif

#else

#if defined(HALIDE_RUNTIME_OPENCL)
#error "TEST_OPENCL not defined but HALIDE_RUNTIME_OPENCL defined"
#endif
#if defined(HALIDE_RUNTIME_CUDA)
#error "TEST_CUDA not defined but HALIDE_RUNTIME_CUDA defined"
#endif
#if defined(HALIDE_RUNTIME_WEBGPU)
#error "TEST_WEBGPU not defined but HALIDE_RUNTIME_WEBGPU defined"
#endif

#endif

int main(int argc, char **argv) {
#if defined(TEST_OPENCL) || defined(TEST_CUDA) || defined(TEST_METAL) || defined(TEST_WEBGPU)
    const int W = 32, H = 32;
    Buffer<int, 2> input(W, H);
    for (int y = 0; y < input.height(); y++) {
        for (int x = 0; x < input.width(); x++) {
            input(x, y) = x + y;
        }
    }

    // Explicitly copy data to the GPU.
    const halide_device_interface_t *interface = nullptr;
#if defined(TEST_OPENCL)
    interface = halide_opencl_device_interface();
#elif defined(TEST_CUDA)
    interface = halide_cuda_device_interface();
#elif defined(TEST_METAL)
    interface = halide_metal_device_interface();
#elif defined(TEST_WEBGPU)
    interface = halide_webgpu_device_interface();
#endif

    Buffer<int, 2> output(W, H);

    input.set_host_dirty();
    input.copy_to_device(interface);
    output.device_malloc(interface);

    // Create halide_buffer_ts without host pointers.
    halide_buffer_t input_no_host = *((halide_buffer_t *)input);
    input_no_host.host = nullptr;

    halide_buffer_t output_no_host = *((halide_buffer_t *)output);
    output_no_host.host = (uint8_t *)nullptr;

    gpu_only(&input_no_host, &output_no_host);

    // Restore the host pointer and copy to host.
    output_no_host.host = (uint8_t *)output.data();
    halide_copy_to_host(nullptr, &output_no_host);

    // Verify output.
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            if (input(x, y) * 2 != output(x, y)) {
                printf("Error at %d, %d: %d != %d\n", x, y, input(x, y), output(x, y));
                return 1;
            }
        }
    }

    printf("Success!\n");
#else
    printf("[SKIP] No GPU target enabled.\n");
#endif
    return 0;
}
