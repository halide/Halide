#include "fcam/Demosaic.h"
#include "fcam/Demosaic_ARM.h"

#include "benchmark.h"
#include "curved.h"
#include "halide_image.h"
#include "halide_image_io.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cassert>

using namespace Halide::Tools;

int main(int argc, char **argv) {
    if (argc < 7) {
        printf("Usage: ./process raw.png color_temp gamma contrast timing_iterations output.png\n"
               "e.g. ./process raw.png 3200 2 50 5 output.png");
        return 0;
    }

    Image<uint16_t> input = load_image(argv[1]);
    fprintf(stderr, "%d %d\n", input.width(), input.height());
    Image<uint8_t> output(((input.width() - 32)/32)*32, ((input.height() - 24)/32)*32, 3);

    // These color matrices are for the sensor in the Nokia N900 and are
    // taken from the FCam source.
    float _matrix_3200[][4] = {{ 1.6697f, -0.2693f, -0.4004f, -42.4346f},
                                {-0.3576f,  1.0615f,  1.5949f, -37.1158f},
                                {-0.2175f, -1.8751f,  6.9640f, -26.6970f}};

    float _matrix_7000[][4] = {{ 2.2997f, -0.4478f,  0.1706f, -39.0923f},
                                {-0.3826f,  1.5906f, -0.2080f, -25.4311f},
                                {-0.0888f, -0.7344f,  2.2832f, -20.0826f}};
    Image<float> matrix_3200(4, 3), matrix_7000(4, 3);
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 4; j++) {
            matrix_3200(j, i) = _matrix_3200[i][j];
            matrix_7000(j, i) = _matrix_7000[i][j];
        }
    }

    float color_temp = atof(argv[2]);
    float gamma = atof(argv[3]);
    float contrast = atof(argv[4]);
    int timing_iterations = atoi(argv[5]);

    double best;

    best = benchmark(timing_iterations, 1, [&]() {
        curved(color_temp, gamma, contrast,
               input, matrix_3200, matrix_7000, output);
    });
    fprintf(stderr, "Halide:\t%gus\n", best * 1e6);
    save_image(output, argv[6]);

    best = benchmark(timing_iterations, 1, [&]() {
        FCam::demosaic(input, output, color_temp, contrast, true, 25, gamma);
    });
    fprintf(stderr, "C++:\t%gus\n", best * 1e6);
    save_image(output, "fcam_c.png");

    best = benchmark(timing_iterations, 1, [&]() {;
        FCam::demosaic_ARM(input, output, color_temp, contrast, true, 25, gamma);
    });
    fprintf(stderr, "ASM:\t%gus\n", best * 1e6);
    save_image(output, "fcam_arm.png");

    // Timings on N900 as of SIGGRAPH 2012 camera ready are (best of 10)
    // Halide: 722ms, FCam: 741ms

    return 0;
}
