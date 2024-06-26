#include <chrono>
#include <cstdio>

#include "compositing.h"
#ifndef NO_AUTO_SCHEDULE
#include "compositing_auto_schedule.h"
#endif

#include "HalideBuffer.h"
#include "halide_benchmark.h"
#include "halide_image_io.h"

using namespace Halide::Runtime;
using namespace Halide::Tools;

// MSVC doesn't define this by default
#ifndef M_PI
#define M_PI 3.14159265358979310000
#endif

int main(int argc, char **argv) {
    if (argc < 4) {
        printf("Usage: ./process input.png timing_iterations output.png\n"
               "e.g.: ./process input.png 10 output.png\n");
        return 1;
    }

    Buffer<uint8_t, 3> input = load_and_convert_image(argv[1]);

    Buffer<uint8_t, 3> output(input.width(), input.height(), 4);
    int timing = atoi(argv[2]);

    // Make a ring of colored blobs to composite over the input, each using a different blend mode
    Buffer<uint8_t, 3> blobs[5];
    int op_codes[] = {4, 3, 2, 1, 0};  // These op codes specify the blend mode to use for each layer.
    Buffer<int, 1> ops(op_codes);
    for (int i = 0; i < 5; i++) {
        blobs[i] = Buffer<uint8_t, 3>::make_with_shape_of(input);
        blobs[i].fill(255);
        for (int y = 0; y < blobs[i].height(); y++) {
            for (int x = 0; x < blobs[i].width(); x++) {
                int cx = cos(i * 2 * M_PI / 5) * 300 + input.width() / 2;
                int cy = sin(i * 2 * M_PI / 5) * 300 + input.height() / 2;
                int alpha = std::min(255, std::min(std::max(0, 500 - std::abs(x - cx)), std::max(0, 500 - std::abs(y - cy))));
                blobs[i](x, y, 3) = alpha;
                blobs[i](x, y, 0) = 255;
                blobs[i](x, y, 1) = (255 / 3) * i;
                blobs[i](x, y, 2) = 255 - blobs[i](x, y, 1);
            }
        }
    }

    compositing(input, blobs[0], blobs[1], blobs[2], blobs[3], blobs[4], ops, output);

    // Timing code

    // Manually-tuned version
    double best_manual = benchmark(timing, 1, [&]() {
        compositing(input, blobs[0], blobs[1], blobs[2], blobs[3], blobs[4], ops, output);
        output.device_sync();
    });
    printf("Manually-tuned time: %gms\n", best_manual * 1e3);

#ifndef NO_AUTO_SCHEDULE
    // Auto-scheduled version
    double best_auto = benchmark(timing, 1, [&]() {
        compositing_auto_schedule(input, blobs[0], blobs[1], blobs[2], blobs[3], blobs[4], ops, output);
        output.device_sync();
    });
    printf("Auto-scheduled time: %gms\n", best_auto * 1e3);
#endif

    convert_and_save_image(output, argv[3]);

    printf("Success!\n");
    return 0;
}
