#include <stdio.h>

#include "HalideBuffer.h"
#include "halide_benchmark.h"
#include "shuffler.h"

using Halide::Runtime::Buffer;
using namespace Halide::Tools;

int main(int argc, char **argv) {
    {
        constexpr int W = 256;

        Buffer<int32_t, 1> input(W);
        for (int x = 0; x < W; x++) {
            input(x) = x;
        }

        Buffer<int32_t, 1> output(W / 4);
        shuffler(input, output);

        for (int x = 0; x < W / 4; x++) {
            int expected = input(input(x / 2 + 1) / 2 + 1) + 1;
            int actual = output(x);
            if (expected != actual) {
                printf("at x = %d expected %d got %d\n", x, expected, actual);
                return -1;
            }
        }
    }

    {
        constexpr int W = 16384;

        Buffer<int32_t, 1> input(W);
        for (int x = 0; x < W; x++) {
            input(x) = x;
        }

        Buffer<int32_t, 1> output(W / 4);

        BenchmarkConfig config;
        config.min_time = 1.0;
        config.max_time = config.min_time * 4;
        double best = benchmark([&]() {
            shuffler(input, output);
        },
                                config);

        printf("Best time: %f\n", best);
    }

    printf("Success!\n");
    return 0;
}
