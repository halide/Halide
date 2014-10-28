#include <Halide.h>
#include <stdio.h>
#include "clock.h"

using namespace Halide;

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

    Image<float> out_fast(8), out_slow(8);

    double t1 = current_time();
    slow.realize(out_slow);
    double t2 = current_time();
    fast.realize(out_fast);
    double t3 = current_time();

    double fast_time = 1e6 * (t3 - t2) / (out_fast.width() * N);
    double slow_time = 1e6 * (t2 - t1) / (out_slow.width() * N);

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
