#include <cassert>
#include <cstdio>
#include <cstdlib>

#include "HalideBuffer.h"
#include "HalideRuntime.h"

#include "interpolate.h"
#include "interpolate_auto_schedule.h"

#include "halide_benchmark.h"
#include "halide_image_io.h"

using namespace Halide::Tools;

int main(int argc, char **argv) {
    if (argc != 3) {
        printf("Usage: %s in out\n", argv[0]);
        return 1;
    }

    Halide::Runtime::Buffer<float, 3> input = load_and_convert_image(argv[1]);
    Halide::Runtime::Buffer<float, 3> output(input.width(), input.height(), 3);

    auto [manual, auto_scheduled] = benchmark_comparison(
        BenchmarkConfig{},
        [&]() {
            interpolate(input, output);
            output.device_sync();
        },
        [&]() {
            interpolate_auto_schedule(input, output);
            output.device_sync();
        });
    printf("Manually-tuned time: %gms\n", manual.wall_time * 1e3);
    printf("Auto-scheduled time: %gms\n", auto_scheduled.wall_time * 1e3);

    convert_and_save_image(output, argv[2]);

    printf("Success!\n");
    return 0;
}
