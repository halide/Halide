#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <iostream>

#include "HalideBuffer.h"
#include "HalideRuntime.h"

#include "depthwise_conv_llvm.h"
#include "depthwise_conv_halide.h"
#include "depthwise_conv_pitchfork.h"
#include "depthwise_conv_rake.h"

#include "halide_benchmark.h"
#include "halide_image_io.h"

using namespace Halide::Tools;

int main(int argc, char **argv) {
    if (argc != 7) {
        printf("Usage: ./run N CI CO W H timing_iterations\n");
        return -1;
    }

    const int N = atoi(argv[1]);
    const int CI = atoi(argv[2]);
    const int CO = atoi(argv[3]);
    // const int CM = atoi(argv[4]);
    const int W = atoi(argv[4]);
    const int H = atoi(argv[5]);
    const int timing_iterations = atoi(argv[6]);

    Halide::Runtime::Buffer<uint8_t> input(CI, W, H, N);
    Halide::Runtime::Buffer<uint8_t> filter(CO, W, H);
    Halide::Runtime::Buffer<int32_t> bias(CO);

    for (int c = 0; c < input.dim(3).extent(); c++) {
        for (int z = 0; z < input.channels(); z++) {
            for (int y = 0; y < input.height(); y++) {
                for (int x = 0; x < input.width(); x++) {
                    input(x, y, z, c) = rand();
                }
            }
        }
    }

    for (int c = 0; c < filter.width(); c++) {
        for (int y = 0; y < filter.height(); y++) {
            for (int z = 0; z < filter.channels(); z++) {
                filter(c, y, z) = rand();
            }
        }
    }

    for (int x = 0; x < bias.width(); x++) {
        bias(x) = rand();
    }

    Halide::Runtime::Buffer<uint8_t> output_llvm(CO, W, H, N);
    Halide::Runtime::Buffer<uint8_t> output_halide(CO, W, H, N);
    Halide::Runtime::Buffer<uint8_t> output_pitchfork(CO, W, H, N);
    Halide::Runtime::Buffer<uint8_t> output_rake(CO, W, H, N);


    const uint8_t input_zero = 3;
    const uint8_t filter_zero = 5;
    const int depth_multiplier = CI / CO;
    const int stride_x = 1;
    const int stride_y = 1;
    const int dilation_x = 0;
    const int dilation_y = 0;
    const int32_t output_multiplier = 32767;
    const uint32_t output_shift = 1;
    const uint8_t output_zero = 3;
    const uint8_t output_min = 5;
    const uint8_t output_max = 250;


    depthwise_conv_llvm(input, input_zero, filter, filter_zero, bias, depth_multiplier, stride_x, stride_y, dilation_x, dilation_y, output_multiplier, output_shift, output_zero, output_min, output_max, output_llvm);

    double min_t_manual = benchmark(timing_iterations, 10, [&]() {
        depthwise_conv_llvm(input, input_zero, filter, filter_zero, bias, depth_multiplier, stride_x, stride_y, dilation_x, dilation_y, output_multiplier, output_shift, output_zero, output_min, output_max, output_llvm);
        output_llvm.device_sync();
    });
    printf("LLVM time: %gms\n", min_t_manual * 1e3);

    depthwise_conv_halide(input, input_zero, filter, filter_zero, bias, depth_multiplier, stride_x, stride_y, dilation_x, dilation_y, output_multiplier, output_shift, output_zero, output_min, output_max, output_halide);

    min_t_manual = benchmark(timing_iterations, 10, [&]() {
        depthwise_conv_halide(input, input_zero, filter, filter_zero, bias, depth_multiplier, stride_x, stride_y, dilation_x, dilation_y, output_multiplier, output_shift, output_zero, output_min, output_max, output_halide);
        output_halide.device_sync();
    });
    printf("Halide time: %gms\n", min_t_manual * 1e3);

    depthwise_conv_pitchfork(input, input_zero, filter, filter_zero, bias, depth_multiplier, stride_x, stride_y, dilation_x, dilation_y, output_multiplier, output_shift, output_zero, output_min, output_max, output_pitchfork);

    min_t_manual = benchmark(timing_iterations, 10, [&]() {
        depthwise_conv_pitchfork(input, input_zero, filter, filter_zero, bias, depth_multiplier, stride_x, stride_y, dilation_x, dilation_y, output_multiplier, output_shift, output_zero, output_min, output_max, output_pitchfork);
        output_pitchfork.device_sync();
    });
    printf("Pitchfork time: %gms\n", min_t_manual * 1e3);

    depthwise_conv_rake(input, input_zero, filter, filter_zero, bias, depth_multiplier, stride_x, stride_y, dilation_x, dilation_y, output_multiplier, output_shift, output_zero, output_min, output_max, output_rake);

    min_t_manual = benchmark(timing_iterations, 10, [&]() {
        depthwise_conv_rake(input, input_zero, filter, filter_zero, bias, depth_multiplier, stride_x, stride_y, dilation_x, dilation_y, output_multiplier, output_shift, output_zero, output_min, output_max, output_rake);
        output_rake.device_sync();
    });
    printf("Rake time: %gms\n", min_t_manual * 1e3);

    for (int i = 0; i < CO; i++) {
        for (int j = 0; j < W; j++) {
            for (int k = 0; k < H; k++) {
                for (int m = 0; m < N; m++) {
                    if (output_llvm(i, j, k, m) != output_halide(i, j, k, m)) {
                        std::cerr << "Halide failure at pixel i=" << i << ", j=" << j << ", k=" << k << ", m=" << m << ": "
                                << (int)output_llvm(i, j, k, m) << " != " << (int)output_halide(i, j, k, m) << "\n";
                        return -1;
                    }

                    if (output_llvm(i, j, k, m) != output_pitchfork(i, j, k, m)) {
                        std::cerr << "Pitchfork failure at pixel i=" << i << ", j=" << j << ", k=" << k << ", m=" << m << ": "
                                << (int)output_llvm(i, j, k, m) << " != " << (int)output_pitchfork(i, j, k, m) << "\n";
                        return -1;
                    }

                    if (output_llvm(i, j, k, m) != output_rake(i, j, k, m)) {
                        std::cerr << "Rake failure at pixel i=" << i << ", j=" << j << ", k=" << k << ", m=" << m << ": "
                                << (int)output_llvm(i, j, k, m) << " != " << (int)output_rake(i, j, k, m) << "\n";
                        return -1;
                    }
                }
            }
        }
    }

    printf("Success!\n");
    return 0;
}
