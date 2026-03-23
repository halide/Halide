#include "HalideBuffer.h"
#include "HalideRuntime.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "blur2x2.h"

#define RUN_BENCHMARKS 0
#if RUN_BENCHMARKS
#include "halide_benchmark.h"
#endif

using namespace Halide::Runtime;

const int W = 80, H = 80;

Buffer<float, 3> buffer_factory_planar(int w, int h, int c) {
    return Buffer<float, 3>(w, h, c);
}

Buffer<float, 3> buffer_factory_interleaved(int w, int h, int c) {
    return Buffer<float, 3>::make_interleaved(w, h, c);
}

void test(Buffer<float, 3> (*factory)(int w, int h, int c)) {
    Buffer<float, 3> input = factory(W, H, 3);
    input.for_each_element([&](int x, int y, int c) {
        // Just an arbitrary color pattern with enough variation to notice the blur
        if (c == 0) {
            input(x, y, c) = ((x % 7) + (y % 3)) / 255.f;
        } else if (c == 1) {
            input(x, y, c) = (x + y) / 255.f;
        } else {
            input(x, y, c) = ((x * 5) + (y * 2)) / 255.f;
        }
    });
    Buffer<float, 3> output = factory(W, H, 3);

    printf("Evaluating output over %d x %d\n", W, H);
    blur2x2(input, W, H, output);

#if RUN_BENCHMARKS
    double t = Halide::Tools::benchmark(10, 100, [&]() {
        blur2x2(input, W, H, output);
    });
    const float megapixels = (W * H) / (1024.f * 1024.f);
    printf("Benchmark: %d %d -> %f mpix/s\n", W, H, megapixels / t);
#endif
}

int main(int argc, char **argv) {
    printf("Testing planar buffer...\n");
    test(buffer_factory_planar);

    printf("Testing interleaved buffer...\n");
    test(buffer_factory_interleaved);

    printf("Success!\n");
    return 0;
}
