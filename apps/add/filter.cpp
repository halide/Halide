#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <iostream>

#include "HalideBuffer.h"
#include "HalideRuntime.h"

#include "add_llvm.h"
#include "add_halide.h"
#include "add_pitchfork.h"
#include "add_rake.h"

#include "halide_benchmark.h"
#include "halide_image_io.h"

using namespace Halide::Tools;

int main(int argc, char **argv) {
    if (argc != 5) {
        printf("Usage: ./run input0.png input1.png output.png timing_iterations\n");
        return -1;
    }

    Halide::Runtime::Buffer<uint8_t> input0 = load_and_convert_image(argv[1]);
    Halide::Runtime::Buffer<uint8_t> input1 = load_and_convert_image(argv[2]);
    Halide::Runtime::Buffer<uint8_t> output_llvm(input0.width(), input0.height());
    Halide::Runtime::Buffer<uint8_t> output_pitchfork(input0.width(), input0.height());
    Halide::Runtime::Buffer<uint8_t> output_halide(input0.width(), input0.height());
    Halide::Runtime::Buffer<uint8_t> output_rake(input0.width(), input0.height());

    int timing_iterations = atoi(argv[4]);

    add_llvm(input0, 0, 100, input1, 0, 100, 0, 5, 225, output_llvm);

    double min_t_manual = benchmark(timing_iterations, 10, [&]() {
        add_llvm(input0, 0, 100, input1, 0, 100, 0, 5, 225, output_llvm);
        output_llvm.device_sync();
    });
    printf("LLVM time: %gms\n", min_t_manual * 1e3);

    add_halide(input0, 0, 100, input1, 0, 100, 0, 5, 225, output_halide);

    min_t_manual = benchmark(timing_iterations, 10, [&]() {
        add_halide(input0, 0, 100, input1, 0, 100, 0, 5, 225, output_halide);
        output_halide.device_sync();
    });
    printf("Halide time: %gms\n", min_t_manual * 1e3);

    add_pitchfork(input0, 0, 100, input1, 0, 100, 0, 5, 225, output_pitchfork);

    min_t_manual = benchmark(timing_iterations, 10, [&]() {
        add_pitchfork(input0, 0, 100, input1, 0, 100, 0, 5, 225, output_pitchfork);
        output_pitchfork.device_sync();
    });
    printf("Pitchfork time: %gms\n", min_t_manual * 1e3);

    add_rake(input0, 0, 100, input1, 0, 100, 0, 5, 225, output_rake);

    min_t_manual = benchmark(timing_iterations, 10, [&]() {
        add_rake(input0, 0, 100, input1, 0, 100, 0, 5, 225, output_rake);
        output_rake.device_sync();
    });
    printf("Rake time: %gms\n", min_t_manual * 1e3);

    for (int i = 0; i < input0.width(); i++) {
        for (int j = 0; j < input0.height(); j++) {
            if (output_llvm(i, j) != output_halide(i, j)) {
                std::cerr << "Halide failure at pixel i=" << i << ", j=" << j << ": "
                          << (int)output_llvm(i, j) << " != " << (int)output_halide(i, j) << "\n";
                return -1;
            }

            if (output_llvm(i, j) != output_pitchfork(i, j)) {
                std::cerr << "Pitchfork failure at pixel i=" << i << ", j=" << j << ": "
                          << (int)output_llvm(i, j) << " != " << (int)output_pitchfork(i, j) << "\n";
                return -1;
            }

            if (output_llvm(i, j) != output_rake(i, j)) {
                std::cerr << "Rake failure at pixel i=" << i << ", j=" << j << ": "
                          << (int)output_llvm(i, j) << " != " << (int)output_rake(i, j) << "\n";
                return -1;
            }
        }
    }

    convert_and_save_image(output_pitchfork, argv[3]);

    printf("Success!\n");
    return 0;
}
