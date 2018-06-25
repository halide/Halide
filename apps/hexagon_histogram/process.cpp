#include <stdio.h>
#include <memory.h>
#include <assert.h>
#include <stdlib.h>
#include "pipeline.h"
#include "HalideBuffer.h"
#include "process.h"

#ifdef HL_HEXAGON_DEVICE
#include "halide_benchmark.h"
#include "HalideRuntimeHexagonHost.h"
#include "halide_image_io.h"
using namespace Halide::Runtime;
using namespace Halide::Tools;
#else
#include "simulator_benchmark.h"
#include "io.h"
#endif

void printInputRange(Halide::Runtime::Buffer<uint16_t> &in, int x0, int x1) {
    uint16_t* in_buf = in.begin();
    for (int x = x0; x <= x1; x++)
        printf("(%3d) = %3d\n", x, in_buf[x]);
}

// Verify result for the halide pipeline
int checker(Halide::Runtime::Buffer<uint16_t> &in, Halide::Runtime::Buffer<HIST_TYPE> &out) {
    // Algorithm
    printf("Checking...\n");
    uint16_t* in_buf = in.begin();
    HIST_TYPE* out_buf = out.begin();

#ifdef HL_HEXAGON_DEVICE
    Halide::Runtime::Buffer<HIST_TYPE> calc(nullptr, HIST_SIZE);
    calc.device_malloc(halide_hexagon_device_interface());
    HIST_TYPE* calc_ptr = calc.begin();
#else
    HIST_TYPE *calc_ptr = (HIST_TYPE *)memalign(1 << LOG2VLEN, HIST_SIZE*sizeof(HIST_TYPE));
#endif

    for (int x = 0; x < HIST_SIZE; x++) {
        calc_ptr[x] = 0;
    }
    for (int x = 0; x < IMG_SIZE; x++) {
        uint16_t idx = in_buf[x];
        // Accumulator
        calc_ptr[idx] += 1;
    }
    for (int x = 0; x < HIST_SIZE; x++) {
        if (out_buf[x] != calc_ptr[x]) {
            printf("Mismatch at %3d (x): %3lu (Halide) == %3lu (Expected)\n",
                        x,
                        (long unsigned int) out_buf[x],
                        (long unsigned int) calc_ptr[x]);
            return 0;
        }
    }
    return 1;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: %s (iterations)\n", argv[0]);
        return 0;
    }
    int iterations = atoi(argv[1]);

#ifdef HL_HEXAGON_DEVICE
    // Image data
    Halide::Runtime::Buffer<uint16_t> in(nullptr, IMG_SIZE);
    // Histogram accumulators
    Halide::Runtime::Buffer<HIST_TYPE> out(nullptr, HIST_SIZE);
    // Hexagon's device_malloc implementation will also set the host
    // pointer if it is null, giving a zero copy buffer.
    in.device_malloc(halide_hexagon_device_interface());
    out.device_malloc(halide_hexagon_device_interface());

#else
    // Image data
    uint16_t *in_ptr  = (uint16_t *)memalign(1 << LOG2VLEN, IMG_SIZE*sizeof(uint16_t));
    // Histogram accumulators
    HIST_TYPE *out_ptr = (HIST_TYPE *)memalign(1 << LOG2VLEN, HIST_SIZE*sizeof(HIST_TYPE));

    Halide::Runtime::Buffer<uint16_t> in(in_ptr, IMG_SIZE);
    Halide::Runtime::Buffer<HIST_TYPE> out(out_ptr, HIST_SIZE);
#endif

    srand(0);
    in.for_each_value([&](uint16_t &x) {
        /* Fill the index array with random data for the histogram */
        x = static_cast<uint16_t>(rand() % HIST_SIZE);
    });

#ifdef HL_HEXAGON_DEVICE
    // To avoid the cost of powering HVX on in each call of the
    // pipeline, power it on once now. Also, set Hexagon performance to turbo.
    halide_hexagon_set_performance_mode(NULL, halide_hexagon_power_turbo);
    halide_hexagon_power_hvx_on(NULL);
#endif

    printf("Running pipeline...\n\n");
    printf("Image size:     %d pixels\n", IMG_SIZE);
    printf("Histogram size: %d bins\n", HIST_SIZE);
    printf("Histogram type: %d bits\n\n", HIST_TYPE_BITS);

#ifdef HL_HEXAGON_DEVICE
    double time = Halide::Tools::benchmark(iterations, 1, [&]() {
#else
    double time = benchmark([&]() {
#endif
        int result = pipeline(in, out);
        if (result != 0) {
            printf("pipeline failed! %d\n", result);
        }
    });
    printf("Done, TIME: %g ms\nTHROUGHPUT: %g MP/s\n", time*1000.0, ((double)(IMG_SIZE))/((double)(1000000) * time));

#ifdef HL_HEXAGON_DEVICE
    // We're done with HVX, power it off, and reset the performance mode
    // to default to save power.
    halide_hexagon_power_hvx_off(NULL);
    halide_hexagon_set_performance_mode(NULL, halide_hexagon_power_default);
#endif

#if 1
    if (!checker(in, out)) {
        printf("Fail!\n");
        return 1;
    }
#endif
    printf("Success!\n");

    return 0;
}
