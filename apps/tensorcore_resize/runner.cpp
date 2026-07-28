#include "Halide.h"
#include "HalideBuffer.h"
#include "HalideRuntimeCuda.h"
#include "halide_benchmark.h"
#include <cstdio>

#include "resize_cudaonly.h"
#include "resize_tensorcore.h"

using Halide::float16_t;
using Halide::Runtime::Buffer;
using Halide::Tools::benchmark;

int main(int argc, char **argv) {
    const auto *interface = halide_cuda_device_interface();
    int major, minor;
    if (interface->compute_capability(nullptr, &major, &minor) != 0 ||
        major * 10 + minor < 70) {
        printf("[SKIP] Tensor cores require CUDA compute capability 7.0 or above.\n");
        return 0;
    }

    const int in_w = 3840, in_h = 2160;
    const float scale_factor = 0.25f;
    const int out_w = (int)(in_w * scale_factor), out_h = (int)(in_h * scale_factor);

    Buffer<float16_t, 3> input(in_w, in_h, 3);
    input.fill([]() { return float16_t((float)rand() / RAND_MAX); });

    Buffer<float16_t, 3> out_cuda(out_w, out_h, 3), out_tensorcore(out_w, out_h, 3);

    resize_cudaonly(input, scale_factor, out_cuda);
    resize_tensorcore(input, scale_factor, out_tensorcore);
    out_cuda.copy_to_host();
    out_tensorcore.copy_to_host();

    // The two schedules compute the same thing, but accumulate in a different
    // order in half precision, so only compare them approximately.
    int bad = 0;
    for (int c = 0; c < 3; c++) {
        for (int y = 0; y < out_h; y++) {
            for (int x = 0; x < out_w; x++) {
                float a = (float)out_cuda(x, y, c), b = (float)out_tensorcore(x, y, c);
                if (std::abs(a - b) > 5e-3f) {
                    if (bad++ < 10) {
                        printf("Mismatch at %d %d %d: %f != %f\n", x, y, c, a, b);
                    }
                }
            }
        }
    }
    if (bad) {
        printf("Failed with %d mismatches\n", bad);
        return 1;
    }

    double t_cuda = benchmark([&]() {
        resize_cudaonly(input, scale_factor, out_cuda);
        out_cuda.device_sync();
    });
    double t_tensorcore = benchmark([&]() {
        resize_tensorcore(input, scale_factor, out_tensorcore);
        out_tensorcore.device_sync();
    });

    printf("cuda only:   %8.3f ms\n", t_cuda * 1e3);
    printf("tensor core: %8.3f ms\n", t_tensorcore * 1e3);
    printf("speed-up:    %8.2fx\n", t_cuda / t_tensorcore);

    printf("Success!\n");
    return 0;
}
