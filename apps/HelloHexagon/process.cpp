#include <stdio.h>
#include <memory.h>
#include <assert.h>
#include <stdlib.h>
#include <malloc.h>

#include "../support/benchmark.h"

#include "pipeline_cpu.h"
#include "pipeline_hvx64.h"
#include "pipeline_hvx128.h"

#include "HalideRuntimeHexagonHost.h"

// Helper function to access element (x, y, z, w) of a buffer_t.
template <typename T>
T& BufferAt(buffer_t* buf,
            int x,
            int y = 0,
            int z = 0,
            int w = 0) {
  return *reinterpret_cast<T*>(
      buf->host + buf->elem_size * ((x - buf->min[0]) * buf->stride[0] +
                                    (y - buf->min[1]) * buf->stride[1] +
                                    (z - buf->min[2]) * buf->stride[2] +
                                    (w - buf->min[3]) * buf->stride[3]));
}

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

    int (*pipeline)(buffer_t *, buffer_t*);
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

    // Set up the buffer_t describing the input buffer.
    buffer_t in = { 0 };
    in.elem_size = 1;
    in.extent[0] = W;
    in.extent[1] = H;
    in.extent[2] = 3;
    in.stride[0] = 1;
    in.stride[1] = W;
    in.stride[2] = W * H;

    // The output buffer has the same description.
    buffer_t out = in;

    // Hexagon's device_malloc implementation will also set the host
    // pointer if it is null, giving a zero copy buffer.
    halide_device_malloc(nullptr, &in, halide_hexagon_device_interface());
    halide_device_malloc(nullptr, &out, halide_hexagon_device_interface());

    // Fill the input buffer with random data.
    for (int i = 0; i < W * H * 3; i++) {
        in.host[i] = static_cast<uint8_t>(rand());
    }

    // To avoid the cost of powering HVX on in each call of the
    // pipeline, power it on once now.
    halide_hexagon_power_hvx_on(NULL);

    printf("Running pipeline...\n");
    double time = benchmark(iterations, 10, [&]() {
        int result = pipeline(&in, &out);
        if (result != 0) {
            printf("pipeline failed! %d\n", result);
        }
    });

    printf("Done, time: %g s\n", time);

    // We're done with HVX, power it off.
    halide_hexagon_power_hvx_off(NULL);

    // Validate that the algorithm did what we expect.
    const uint16_t gaussian5[] = { 1, 4, 6, 4, 1 };
    for (int c = 0; c < 3; c++) {
        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                uint16_t blur = 0;
                for (int rx = -2; rx <= 2; rx++) {
                    uint16_t blur_y = 0;
                    for (int ry = -2; ry <= 2; ry++) {
                        uint16_t in_rxy =
                            BufferAt<uint8_t>(&in, clamp(x + rx, 0, W - 1), clamp(y + ry, 0, H - 1), c);
                        blur_y += in_rxy * gaussian5[ry + 2];
                    }
                    blur_y += 8;
                    blur_y /= 16;

                    blur += blur_y * gaussian5[rx + 2];
                }
                blur += 8;
                blur /= 16;

                uint8_t out_xy = BufferAt<uint8_t>(&out, x, y, c);
                if (blur != out_xy) {
                    printf("Mismatch at %d %d %d: %d != %d\n", x, y, c, out_xy, blur);
                    return -1;
                }
            }
        }
    }

    halide_device_free(NULL, &in);
    halide_device_free(NULL, &out);

    printf("Success!\n");

    return 0;
}
