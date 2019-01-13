#include <cstdio>
#include <random>

#include "fit_and_slice_3x4.h"
#include "fit_and_slice_3x4_classic_auto_schedule.h"
#include "fit_and_slice_3x4_auto_schedule.h"

#include "benchmark_util.h"
#include "HalideBuffer.h"

int main(int argc, char **argv) {
    if (argc != 1) {
        printf("Usage: %s\n", argv[0]);
        return 1;
    }

    const float r_sigma = 1.f / 8.f;
    const float s_sigma = 16.f;

    Halide::Runtime::Buffer<float> low_res_in(192, 320, 3);
    Halide::Runtime::Buffer<float> low_res_out(192, 320, 3);  // not an output, despite the name
    Halide::Runtime::Buffer<float> high_res_in(1536, 2560, 3);
    Halide::Runtime::Buffer<float> high_res_out(1536, 2560, 3);

    constexpr uint32_t seed = 0;
    std::mt19937 rng(seed);
    low_res_in.for_each_value([&rng](float &f) {
        f = ((float)rng()) / rng.max() - 0.5f;
    });
    low_res_out.for_each_value([&rng](float &f) {
        f = ((float)rng()) / rng.max() - 0.5f;
    });
    high_res_in.for_each_value([&rng](float &f) {
        f = ((float)rng()) / rng.max() - 0.5f;
    });

    multi_way_bench({
        {"Manual", [&]() { fit_and_slice_3x4(r_sigma, s_sigma, low_res_in, low_res_out, high_res_in, high_res_out); }},
        {"Classic auto-scheduled", [&]() { fit_and_slice_3x4_classic_auto_schedule(r_sigma, s_sigma, low_res_in, low_res_out, high_res_in, high_res_out); }},
        {"Auto-scheduled", [&]() { fit_and_slice_3x4_auto_schedule(r_sigma, s_sigma, low_res_in, low_res_out, high_res_in, high_res_out); }},
        {"Simple auto-scheduled", [&]() { fit_and_slice_3x4_simple_auto_schedule(r_sigma, s_sigma, low_res_in, low_res_out, high_res_in, high_res_out); }}
    });

    return 0;
}
