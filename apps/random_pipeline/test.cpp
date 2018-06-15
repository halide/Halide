#include <cmath>
#include <cstdint>
#include <cstdio>
#include <atomic>

#include "halide_benchmark.h"
#include "HalideBuffer.h"

using namespace Halide::Runtime;
using namespace Halide::Tools;

#include "random_pipeline.h"

typedef void *(*halide_malloc_t)(void *, size_t);
typedef void (*halide_free_t)(void *, void *);
extern halide_malloc_t halide_set_custom_malloc(halide_malloc_t user_malloc);
extern halide_free_t halide_set_custom_free(halide_free_t user_free);

std::atomic<int64_t> counter;
size_t peak;

void *my_malloc(void *ucon, size_t sz) {
    counter++;
    if (sz > peak) peak = sz;

    return halide_default_malloc(ucon, sz);
}

int main(int argc, char **argv) {
    Buffer<float> output(2000, 2000, 3);

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
    input.fill(0.0f);

    printf("Input size: %d %d %d\n", input.width(), input.height(), input.channels());

    double best = benchmark([&]() {
        random_pipeline(input, output);
    });
    printf("Time: %g\n", best * 1e3);

    /*
    counter = 0;
    peak = 0;
    halide_set_custom_malloc(my_malloc);
    random_pipeline(input, output);
    printf("Mallocs: %lld %lld\n", (long long)counter, (long long)peak);
    */

    return 0;
}
