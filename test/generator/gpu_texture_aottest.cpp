#include "HalideBuffer.h"
#include "HalideRuntime.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#if defined(TEST_OPENCL)
#include "HalideRuntimeOpenCL.h"
#endif
#if defined(TEST_D3D12COMPUTE)
#include "HalideRuntimeD3D12Compute.h"
#endif

#include "gpu_texture.h"
using namespace Halide::Runtime;

#if defined(TEST_OPENCL)
#if !defined(HALIDE_RUNTIME_OPENCL)
#error "TEST_OPENCL defined but HALIDE_RUNTIME_OPENCL not defined"
#endif
#endif

#if defined(TEST_D3D12COMPUTE)
#if !defined(HALIDE_RUNTIME_D3D12COMPUTE)
#error "TEST_D3D12COMPUTE defined but HALIDE_RUNTIME_D3D12COMPUTE not defined"
#endif
#endif

int main(int argc, char **argv) {
#if !defined(TEST_OPENCL) && !defined(TEST_D3D12COMPUTE)
    printf("[SKIP] No GPU texture target enabled.\n");
    return 0;
#endif

#if defined(TEST_OPENCL)
    {
        const auto *interface = halide_opencl_device_interface();
        assert(interface->compute_capability != nullptr);
        int major, minor;
        int err = interface->compute_capability(nullptr, &major, &minor);
        if (err != 0 || (major == 1 && minor < 2)) {
            printf("[SKIP] OpenCL %d.%d is less than required 1.2.\n", major, minor);
            return 0;
        }
    }
#endif

    const int W = 32, H = 32;
    Buffer<int, 2> input(W, H);
    for (int y = 0; y < input.height(); y++) {
        for (int x = 0; x < input.width(); x++) {
            input(x, y) = x + y;
        }
    }

    // Explicitly copy data to the GPU.
    input.set_host_dirty();

    Buffer<int, 2> output(W, H);

    gpu_texture(input, output);

#if defined(TEST_OPENCL)
    if (input.raw_buffer()->device_interface != halide_opencl_image_device_interface()) {
        printf("Expected input to be copied to OpenCL texture storage\n");
        return 1;
    }
    if (output.raw_buffer()->device_interface != halide_opencl_image_device_interface()) {
        printf("Expected output to be copied to OpenCL texture storage\n");
        return 1;
    }
#elif defined(TEST_D3D12COMPUTE)
    if (input.raw_buffer()->device_interface != halide_d3d12compute_image_device_interface()) {
        printf("Expected input to be copied to D3D12 texture storage\n");
        return 1;
    }
    if (output.raw_buffer()->device_interface != halide_d3d12compute_image_device_interface()) {
        printf("Expected output to be copied to D3D12 texture storage\n");
        return 1;
    }
#endif

    output.copy_to_host();

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
    return 0;
}
