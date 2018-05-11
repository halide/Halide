#include <cmath>
#include <cstdint>
#include <cstdio>

#include "halide_benchmark.h"
#include "HalideBuffer.h"

using namespace Halide::Runtime;
using namespace Halide::Tools;

#include "random_pipeline.h"
#include "random_pipeline_auto_schedule.h"

int main(int argc, char **argv) {
    Buffer<float> output(1024, 1024, 3);

    for (int y = 0; y < output.height(); y++) {
        for (int x = 0; x < output.width(); x++) {
            for (int c = 0; c < output.channels(); c++) {
                output(x, y, c) = rand() & 0xfff;
            }
        }
    }

    Buffer<float> input;
    assert(input.is_bounds_query());
    random_pipeline(input, output);
    input.allocate();

    double best_manual = benchmark(10, 10, [&]() {
        random_pipeline(input, output);
    });
    printf("Manually-tuned time: %gms\n", best_manual * 1e3);

    double best_auto = benchmark(10, 10, [&]() {
        random_pipeline_auto_schedule(input, output);
    });
    printf("Auto-scheduled time: %gms\n", best_auto * 1e3);

    return 0;
}
