#include <cstdio>
#include <chrono>

#include "shallow_lower_bound.h"
#include "shallow_lower_bound_auto_schedule.h"

#include "halide_benchmark.h"
#include "HalideBuffer.h"
#include "halide_image_io.h"

using namespace Halide::Runtime;
using namespace Halide::Tools;

int main(int argc, char **argv) {
    Halide::Runtime::Buffer<float> input(256, 256, 256);
    Halide::Runtime::Buffer<uint8_t> valid_u8(256, 256, 256);

    for (int c = 0; c < input.dim(3).extent(); c++) {
        for (int z = 0; z < input.channels(); z++) {
            for (int y = 0; y < input.height(); y++) {
                for (int x = 0; x < input.width(); x++) {
                    input(x, y) = rand();
                }
            }
        }
    }
    for (int c = 0; c < valid_u8.dim(3).extent(); c++) {
        for (int z = 0; z < valid_u8.channels(); z++) {
            for (int y = 0; y < valid_u8.height(); y++) {
                for (int x = 0; x < valid_u8.width(); x++) {
                    valid_u8(x, y) = rand();
                }
            }
        }
    }

    const int r = 128;
    const int radius_x = r;
    const int radius_y = r;
    const int radius_z = r;

    Halide::Runtime::Buffer<float> output(256, 256, 256);

    shallow_lower_bound(input, valid_u8, radius_x, radius_y, radius_z, output);

    // Timing code

    // Manually-tuned version
    double best_manual = benchmark(20, 10, [&]() {
        shallow_lower_bound(input, valid_u8, radius_x, radius_y, radius_z, output);
    });
    printf("Manually-tuned time: %gms\n", best_manual * 1e3);

    // Auto-scheduled version
    double best_auto = benchmark(20, 10, [&]() {
        shallow_lower_bound_auto_schedule(input, valid_u8, radius_x, radius_y, radius_z, output);
    });
    printf("Auto-scheduled time: %gms\n", best_auto * 1e3);

    return 0;
}
