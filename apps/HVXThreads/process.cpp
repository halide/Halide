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
using namespace std;
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

template<typename T>
T Clamp(T v, T a, T b) {
    if (v < a) return a;
    if (v > b) return b;
    return v;
}
int main(int argc, char **argv) {
    if (argc < 3) {
        printf("Usage: %s (cpu|hvx64) timing_iterations\n", argv[0]);
        return 0;
    }

    int (*pipeline)(buffer_t *, buffer_t *, buffer_t*);
    if (strcmp(argv[1], "cpu") == 0) {
        pipeline = pipeline_cpu;
        printf("Using CPU schedule\n");
    } else  if (strcmp(argv[1], "hvx64") == 0) {
        pipeline = pipeline_hvx64;
        printf("Using HVX 64 schedule\n");
    } else if (strcmp(argv[1], "hvx128") == 0) {
        pipeline = pipeline_hvx128;
        printf("Using HVX 128 schedule\n");
    }
    else {
        printf("Unknown schedule, valid schedules are cpu, hvx64, or hvx128\n");
        return -1;
    }

    int iterations = atoi(argv[2]);

    const int W = 1024;
    const int H = 1024;

    // Set up the buffer_t describing the input buffer.
    buffer_t in1 = { 0 };
    in1.elem_size = 1;
    in1.extent[0] = W;
    in1.extent[1] = H;
    in1.stride[0] = 1;
    in1.stride[1] = W;

    buffer_t in2 = in1;
    // The output buffer has the same description.
    buffer_t out = in1;

    // Hexagon's device_malloc implementation will also set the host
    // pointer if it is null, giving a zero copy buffer.
    printf ("Allocating device memory\n");
    halide_device_malloc(nullptr, &in1, halide_hexagon_device_interface());
    halide_device_malloc(nullptr, &in2, halide_hexagon_device_interface());
    halide_device_malloc(nullptr, &out, halide_hexagon_device_interface());

    // Fill the input buffer with random data.
    for (int i = 0; i < W * H; i++) {
        in1.host[i] = static_cast<uint8_t>(rand());
        in2.host[i] = static_cast<uint8_t>(rand());
    }

    printf("Running pipeline...\n");
    double time = benchmark(iterations, 10, [&]() {
            int result = pipeline(&in1, &in2, &out);
            if (result != 0) {
                printf("pipeline failed! %d\n", result);
            }
        });

    printf("Done, time: %g s\n", time);

    // Validate that the algorithm did what we expect.
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            uint8_t in1_xy = BufferAt<uint8_t>(&in1, x, y);            
            uint8_t in2_xy = BufferAt<uint8_t>(&in2, x, y);
            uint8_t out_xy = BufferAt<uint8_t>(&out, x, y);
            if (out_xy != Clamp((uint16_t)in1_xy + (uint16_t)in2_xy, 0, 255)) {
                printf ("Mismatch at x = %d, y = %d\n", x, y);
                printf ("out = %d, in1 = %d, in2 = %d\n", out_xy, in1_xy, in2_xy);
                return -1;
            }
        }
    }
    halide_device_free(NULL, &in1);
    halide_device_free(NULL, &in2);
    halide_device_free(NULL, &out);

    printf("Success!\n");

    return 0;
}
