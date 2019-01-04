#include "camera_pipe.h"
#ifndef NO_AUTO_SCHEDULE
// #include "camera_pipe_classic_auto_schedule.h"
#include "camera_pipe_auto_schedule.h"
#endif

#include "benchmark_util.h"
#include "HalideBuffer.h"
#include "halide_image_io.h"
#include "halide_malloc_trace.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cassert>

using namespace Halide::Runtime;
using namespace Halide::Tools;

int main(int argc, char **argv) {
    if (argc < 8) {
        printf("Usage: ./process raw.png color_temp gamma contrast sharpen timing_iterations output.png\n"
               "e.g. ./process raw.png 3200 2 50 5 output.png");
        return 0;
    }

#ifdef HL_MEMINFO
    halide_enable_malloc_trace();
#endif

    fprintf(stderr, "input: %s\n", argv[1]);
    Buffer<uint16_t> input = load_and_convert_image(argv[1]);
    fprintf(stderr, "       %d %d\n", input.width(), input.height());
    Buffer<uint8_t> output(((input.width() - 32)/32)*32, ((input.height() - 24)/32)*32, 3);

#ifdef HL_MEMINFO
    info(input, "input");
    stats(input, "input");
    // dump(input, "input");
#endif

    // These color matrices are for the sensor in the Nokia N900 and are
    // taken from the FCam source.
    float _matrix_3200[][4] = {{ 1.6697f, -0.2693f, -0.4004f, -42.4346f},
                                {-0.3576f,  1.0615f,  1.5949f, -37.1158f},
                                {-0.2175f, -1.8751f,  6.9640f, -26.6970f}};

    float _matrix_7000[][4] = {{ 2.2997f, -0.4478f,  0.1706f, -39.0923f},
                                {-0.3826f,  1.5906f, -0.2080f, -25.4311f},
                                {-0.0888f, -0.7344f,  2.2832f, -20.0826f}};
    Buffer<float> matrix_3200(4, 3), matrix_7000(4, 3);
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 4; j++) {
            matrix_3200(j, i) = _matrix_3200[i][j];
            matrix_7000(j, i) = _matrix_7000[i][j];
        }
    }

    float color_temp = (float) atof(argv[2]);
    float gamma = (float) atof(argv[3]);
    float contrast = (float) atof(argv[4]);
    float sharpen = (float) atof(argv[5]);
    const int samples = atoi(argv[6]);
    const int iterations = 1;
    int blackLevel = 25;
    int whiteLevel = 1023;

    three_way_bench(
        [&]() { camera_pipe(input, matrix_3200, matrix_7000, color_temp, gamma, contrast, sharpen, blackLevel, whiteLevel, output); },
    #ifdef NO_AUTO_SCHEDULE
        nullptr,
        nullptr,
    #else
        // TODO: camera_pipe fails under the classic autoscheduler with an internal error.
        nullptr, // [&]() { camera_pipe_classic_auto_schedule(input, matrix_3200, matrix_7000, color_temp, gamma, contrast, sharpen, blackLevel, whiteLevel, output); },
        [&]() { camera_pipe_auto_schedule(input, matrix_3200, matrix_7000, color_temp, gamma, contrast, sharpen, blackLevel, whiteLevel, output); },
    #endif
        samples,
        iterations
    );

    fprintf(stderr, "output: %s\n", argv[7]);
    convert_and_save_image(output, argv[7]);
    fprintf(stderr, "        %d %d\n", output.width(), output.height());

    return 0;
}
