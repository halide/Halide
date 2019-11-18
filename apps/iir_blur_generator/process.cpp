#include <cstdio>
#include <random>

#include "iir_blur.h"
#include "iir_blur_auto_schedule.h"
#include "iir_blur_gradient_auto_schedule.h"

#include "benchmark_util.h"
#include "HalideBuffer.h"

int main(int argc, char **argv) {
    if (argc != 1) {
        printf("Usage: %s\n", argv[0]);
        return 1;
    }

    const float alpha = 0.1f;

    Halide::Runtime::Buffer<float> input(1536, 2560, 3);
    Halide::Runtime::Buffer<float> output(1536, 2560, 3);

    constexpr uint32_t seed = 0;
    std::mt19937 rng(seed);
    input.for_each_value([&rng](float &f) {
        f = ((float)rng()) / rng.max() - 0.5f;
    });

    multi_way_bench({
        {"Manual", [&]() { iir_blur(input, alpha, output); output.device_sync(); }},
        {"Auto-scheduled", [&]() { iir_blur_auto_schedule(input, alpha, output); output.device_sync(); }},
        {"Gradient auto-scheduled", [&]() { iir_blur_gradient_auto_schedule(input, alpha, output); output.device_sync(); }}
    });

    return 0;
}
