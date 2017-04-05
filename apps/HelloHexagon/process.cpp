#include <stdio.h>
#include <memory.h>
#include <assert.h>
#include <stdlib.h>

#include "halide_benchmark.h"

#include "pipeline_cpu.h"
#include "pipeline_hvx64.h"
#include "pipeline_hvx128.h"

#include "HalideRuntimeHexagonHost.h"
#include "HalideBuffer.h"

template <typename T>
T clamp(T x, T min, T max) {
    if (x < min) x = min;
    if (x > max) x = max;
    return x;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        printf("Usage: %s (cpu|hvx64) timing_iterations\n", argv[0]);
        return 0;
    }

    int (*pipeline)(halide_buffer_t *, halide_buffer_t*);
    if (strcmp(argv[1], "cpu") == 0) {
        pipeline = pipeline_cpu;
        printf("Using CPU schedule\n");
    } else if (strcmp(argv[1], "hvx64") == 0) {
        pipeline = pipeline_hvx64;
        printf("Using HVX 64 schedule\n");
    } else if (strcmp(argv[1], "hvx128") == 0) {
        pipeline = pipeline_hvx128;
        printf("Using HVX 128 schedule\n");
    } else {
        printf("Unknown schedule, valid schedules are cpu, hvx64, or hvx128\n");
        return -1;
    }

    int iterations = atoi(argv[2]);

    const int W = 1024;
    const int H = 1024;


    // Hexagon's device_malloc implementation will also set the host
    // pointer if it is null, giving a zero copy buffer.
    Halide::Runtime::Buffer<uint8_t> in(nullptr, W, H, 3);
    Halide::Runtime::Buffer<uint8_t> out(nullptr, W, H, 3);

    in.device_malloc(halide_hexagon_device_interface());
    out.device_malloc(halide_hexagon_device_interface());

    // Fill the input buffer with random data.
    in.for_each_value([&](uint8_t &x) {
        x = static_cast<uint8_t>(rand());
    });

    // To avoid the cost of powering HVX on in each call of the
    // pipeline, power it on once now. Also, set Hexagon performance to turbo.
    halide_hexagon_set_performance_mode(NULL, halide_hexagon_power_turbo);
    halide_hexagon_power_hvx_on(NULL);

    printf("Running pipeline...\n");
    double time = Halide::Tools::benchmark(iterations, 10, [&]() {
        int result = pipeline(in, out);
        if (result != 0) {
            printf("pipeline failed! %d\n", result);
        }
    });

    printf("Done, time: %g s\n", time);

    // We're done with HVX, power it off, and reset the performance mode
    // to default to save power.
    halide_hexagon_power_hvx_off(NULL);
    halide_hexagon_set_performance_mode(NULL, halide_hexagon_power_default);

    // Validate that the algorithm did what we expect.
    const uint16_t gaussian5[] = { 1, 4, 6, 4, 1 };
    out.for_each_element([&](int x, int y, int c) {
        uint16_t blur = 0;
        for (int rx = -2; rx <= 2; rx++) {
            uint16_t blur_y = 0;
            for (int ry = -2; ry <= 2; ry++) {
                uint16_t in_rxy =
                    in(clamp(x + rx, 0, W - 1), clamp(y + ry, 0, H - 1), c);
                blur_y += in_rxy * gaussian5[ry + 2];
            }
            blur_y += 8;
            blur_y /= 16;

            blur += blur_y * gaussian5[rx + 2];
        }
        blur += 8;
        blur /= 16;

        uint8_t out_xy = out(x, y, c);
        if (blur != out_xy) {
            printf("Mismatch at %d %d %d: %d != %d\n", x, y, c, out_xy, blur);
            abort();
        }

    });

    printf("Success!\n");

    return 0;
}
