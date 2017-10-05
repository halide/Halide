#include "Halide.h"
#include <cstdio>
#include "halide_benchmark.h"

using namespace Halide;
using namespace Halide::Tools;

int main(int argc, char **argv) {
    Func slow, fast;
    Var x;
    Param<float> p(1.0f);

    const int N = 10000000;

    // Compute the golden mean using a continued fraction.
    RDom r(0, N);
    slow(x) = 1.0f;
    fast(x) = 1.0f;
    slow(x) = p / (slow(x) + 1) + 0*r;
    fast(x) = fast_inverse((fast(x) + 1) + 0*r);

    slow.update().vectorize(x, 4);
    fast.update().vectorize(x, 4);

    slow.compile_jit();
    fast.compile_jit();

    Buffer<float> out_fast(8), out_slow(8);

    double slow_time = benchmark(1, 1, [&]() { slow.realize(out_slow); });
    double fast_time = benchmark(1, 1, [&]() { fast.realize(out_fast); });

    slow_time *= 1e9 / (out_fast.width() * N);
    fast_time *= 1e9 / (out_fast.width() * N);

    if (fabs(out_fast(0) - out_slow(0)) > 1e-5) {
        printf("Mismatched answers:\n"
               "fast: %10.10f\n"
               "slow: %10.10f\n",
               out_fast(0), out_slow(0));
        return 1;
    }

    printf("True inverse: %f ns\n"
           "Fast inverse: %f ns\n",
           slow_time, fast_time);

    if (fast_time > slow_time) {
        printf("Fast inverse is slower than true division.\n");
        return 1;
    }

    printf("Success!\n");
    return 0;
}
