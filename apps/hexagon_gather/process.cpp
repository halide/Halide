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
int checker(Halide::Runtime::Buffer<DTYPE> &in,
        Halide::Runtime::Buffer<DTYPE> &lut,
        Halide::Runtime::Buffer<DTYPE> &out) {
    int errcnt = 0, maxerr = 10;
    printf("Checking...\n");
    DTYPE* in_buf = in.begin();
    DTYPE* out_buf = out.begin();
    DTYPE* lut_buf = lut.begin();
    DTYPE *calc_ptr = (DTYPE *)memalign(128, IMG_SIZE*sizeof(DTYPE));

    for (int x = 0; x < IMG_SIZE; x++) {
        DTYPE idx = in_buf[x];
        calc_ptr[x] = lut_buf[idx];
    }
    for (int x = 0; x < IMG_SIZE; x++) {
        if (out_buf[x] != calc_ptr[x]) {
            errcnt++;
            if (errcnt <= maxerr) {
                printf("Mismatch at %3d (x): %3d (Halide) == %3d (Expected)\n",
                        x, out_buf[x], calc_ptr[x]);
            }
        }
    }
    if (errcnt > maxerr) {
        printf("...\n");
    }
    if (errcnt > 0) {
        printf("Mismatch at %d places\n", errcnt);
    }
    return errcnt;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: %s (iterations)\n", argv[0]);
        return 0;
    }
    int iterations = atoi(argv[1]);

#ifdef HL_HEXAGON_DEVICE
    Halide::Runtime::Buffer<DTYPE> in(nullptr, IMG_SIZE);
    Halide::Runtime::Buffer<DTYPE> out(nullptr, IMG_SIZE);
    Halide::Runtime::Buffer<DTYPE> lut(nullptr, TBL_SIZE);

    in.device_malloc(halide_hexagon_device_interface());
    out.device_malloc(halide_hexagon_device_interface());
    lut.device_malloc(halide_hexagon_device_interface());
#else
    DTYPE *in_ptr  = (DTYPE *)memalign(1 << LOG2VLEN, IMG_SIZE*sizeof(DTYPE));
    DTYPE *out_ptr = (DTYPE *)memalign(1 << LOG2VLEN, IMG_SIZE*sizeof(DTYPE));
    DTYPE *lut_ptr = (DTYPE *)memalign(1 << LOG2VLEN, TBL_SIZE*sizeof(DTYPE));

    Halide::Runtime::Buffer<DTYPE> in(in_ptr, IMG_SIZE);
    Halide::Runtime::Buffer<DTYPE> out(out_ptr, IMG_SIZE);
    Halide::Runtime::Buffer<DTYPE> lut(lut_ptr, TBL_SIZE);
#endif

    srand(0);
    // Fill the input image
    in.for_each_value([&](DTYPE &x) {
        x = static_cast<DTYPE>(rand() % TBL_SIZE);
    });

    // Fill the lookup table
    lut.for_each_value([&](DTYPE &x) {
        x = static_cast<DTYPE>(rand());
    });

#ifdef HL_HEXAGON_DEVICE
    // To avoid the cost of powering HVX on in each call of the
    // pipeline, power it on once now. Also, set Hexagon performance to turbo.
    halide_hexagon_set_performance_mode(NULL, halide_hexagon_power_turbo);
    halide_hexagon_power_hvx_on(NULL);
#endif

    printf("Running pipeline...\n\n");
    printf("Image size: %d pixels\n", IMG_SIZE);
    printf("Image type: %d bits\n", (int) sizeof(DTYPE)*8);
    printf("Table size: %d elements\n\n", TBL_SIZE);

#ifdef HL_HEXAGON_DEVICE
    double time = Halide::Tools::benchmark(iterations, 1, [&]() {
#else
    double time = benchmark([&]() {
#endif
        int result = pipeline(in, lut, out);
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
    if (checker(in, lut, out) != 0) {
        printf("Fail!\n");
        return 1;
    }
#endif
    printf("Success!\n");

    return 0;
}
