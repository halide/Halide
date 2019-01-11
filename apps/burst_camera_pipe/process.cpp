#include <cstdio>
#include <random>

#include "burst_camera_pipe.h"
#include "burst_camera_pipe_classic_auto_schedule.h"
#include "burst_camera_pipe_auto_schedule.h"

#include "benchmark_util.h"
#include "halide_image_io.h"
#include "HalideBuffer.h"

int main(int argc, char **argv) {
    if (argc != 1 && argc != 2) {
        printf("Usage: %s [output]\n", argv[0]);
        return 1;
    }

    constexpr int w = 5218;
    constexpr int h = 3482;
    constexpr int num_frames = 7;

    constexpr uint16_t black_point = 2050;
    constexpr uint16_t white_point = 15464;
    constexpr float white_balance_r = 2.29102;
    constexpr float white_balance_g0 = 1;
    constexpr float white_balance_g1 = 1;
    constexpr float white_balance_b = 1.26855;
    constexpr float compression = 3.8;
    constexpr float gain = 1.1;

    Halide::Runtime::Buffer<uint16_t> inputs(w, h, num_frames);
    Halide::Runtime::Buffer<uint8_t> output(w, h, 3);

    constexpr uint32_t seed = 0;
    std::mt19937 rng(seed);
    inputs.for_each_value([&rng](uint16_t &f) {
        f = (uint16_t) rng();
    });

    three_way_bench(
        [&]() { burst_camera_pipe(inputs, black_point, white_point, white_balance_r, white_balance_g0, white_balance_g1, white_balance_b, compression, gain, output); },
        [&]() { burst_camera_pipe_classic_auto_schedule(inputs, black_point, white_point, white_balance_r, white_balance_g0, white_balance_g1, white_balance_b, compression, gain, output); },
        [&]() { burst_camera_pipe_auto_schedule(inputs, black_point, white_point, white_balance_r, white_balance_g0, white_balance_g1, white_balance_b, compression, gain, output); }
    );

    if (argc == 2) {
        Halide::Tools::convert_and_save_image(output, argv[1]);
    }

    return 0;
}
