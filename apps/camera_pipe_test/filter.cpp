#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <iostream>

#include "HalideBuffer.h"
#include "HalideRuntime.h"

#include "camera_pipe_llvm.h"
#include "camera_pipe_halide.h"
#include "camera_pipe_pitchfork.h"
#include "camera_pipe_rake.h"

#include "halide_benchmark.h"
#include "halide_image_io.h"

using namespace Halide::Tools;

int main(int argc, char **argv) {
    if (argc != 8) {
        printf("Usage: ./run raw.png color_temp gamma contrast sharpen timing_iterations output.png\n");
        return 0;
    }

    Halide::Runtime::Buffer<uint16_t> input = load_and_convert_image(argv[1]);
    Halide::Runtime::Buffer<uint8_t> output_llvm(((input.width() - 32) / 32) * 32, ((input.height() - 24) / 32) * 32, 3);
    Halide::Runtime::Buffer<uint8_t> output_halide(((input.width() - 32) / 32) * 32, ((input.height() - 24) / 32) * 32, 3);
    Halide::Runtime::Buffer<uint8_t> output_pitchfork(((input.width() - 32) / 32) * 32, ((input.height() - 24) / 32) * 32, 3);
    Halide::Runtime::Buffer<uint8_t> output_rake(((input.width() - 32) / 32) * 32, ((input.height() - 24) / 32) * 32, 3);

    // These color matrices are for the sensor in the Nokia N900 and are
    // taken from the FCam source.
    float _matrix_3200[][4] = {{1.6697f, -0.2693f, -0.4004f, -42.4346f},
                               {-0.3576f, 1.0615f, 1.5949f, -37.1158f},
                               {-0.2175f, -1.8751f, 6.9640f, -26.6970f}};

    float _matrix_7000[][4] = {{2.2997f, -0.4478f, 0.1706f, -39.0923f},
                               {-0.3826f, 1.5906f, -0.2080f, -25.4311f},
                               {-0.0888f, -0.7344f, 2.2832f, -20.0826f}};
    Halide::Runtime::Buffer<float, 2> matrix_3200(4, 3), matrix_7000(4, 3);
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 4; j++) {
            matrix_3200(j, i) = _matrix_3200[i][j];
            matrix_7000(j, i) = _matrix_7000[i][j];
        }
    }

    float color_temp = (float)atof(argv[2]);
    float gamma = (float)atof(argv[3]);
    float contrast = (float)atof(argv[4]);
    float sharpen = (float)atof(argv[5]);
    int timing_iterations = atoi(argv[6]);
    int blackLevel = 25;
    int whiteLevel = 1023;


    camera_pipe_llvm(input, matrix_3200, matrix_7000, color_temp, gamma, contrast, sharpen, blackLevel, whiteLevel, output_llvm);

    double min_t_manual = benchmark(timing_iterations, 10, [&]() {
        camera_pipe_llvm(input, matrix_3200, matrix_7000, color_temp, gamma, contrast, sharpen, blackLevel, whiteLevel, output_llvm);
        output_llvm.device_sync();
    });
    printf("LLVM time: %gms\n", min_t_manual * 1e3);

    camera_pipe_halide(input, matrix_3200, matrix_7000, color_temp, gamma, contrast, sharpen, blackLevel, whiteLevel, output_halide);

    min_t_manual = benchmark(timing_iterations, 10, [&]() {
        camera_pipe_halide(input, matrix_3200, matrix_7000, color_temp, gamma, contrast, sharpen, blackLevel, whiteLevel, output_halide);
        output_halide.device_sync();
    });
    printf("Halide time: %gms\n", min_t_manual * 1e3);

    camera_pipe_pitchfork(input, matrix_3200, matrix_7000, color_temp, gamma, contrast, sharpen, blackLevel, whiteLevel, output_pitchfork);

    min_t_manual = benchmark(timing_iterations, 10, [&]() {
        camera_pipe_pitchfork(input, matrix_3200, matrix_7000, color_temp, gamma, contrast, sharpen, blackLevel, whiteLevel, output_pitchfork);
        output_pitchfork.device_sync();
    });
    printf("Pitchfork time: %gms\n", min_t_manual * 1e3);

    camera_pipe_rake(input, matrix_3200, matrix_7000, color_temp, gamma, contrast, sharpen, blackLevel, whiteLevel, output_rake);

    min_t_manual = benchmark(timing_iterations, 10, [&]() {
        camera_pipe_rake(input, matrix_3200, matrix_7000, color_temp, gamma, contrast, sharpen, blackLevel, whiteLevel, output_rake);
        output_rake.device_sync();
    });
    printf("Rake time: %gms\n", min_t_manual * 1e3);

    for (int i = 0; i < input.width(); i++) {
        for (int j = 0; j < input.height(); j++) {
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

    convert_and_save_image(output_pitchfork, argv[7]);

    printf("Success!\n");
    return 0;
}
