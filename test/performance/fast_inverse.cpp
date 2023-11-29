#include "Halide.h"
#include "halide_benchmark.h"
#include <cstdio>

using namespace Halide;
using namespace Halide::Tools;

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (target.arch == Target::WebAssembly) {
        printf("[SKIP] Performance tests are meaningless and/or misleading under WebAssembly interpreter.\n");
        return 0;
    }

    if (target.arch == Target::ARM &&
        target.os == Target::OSX) {
        // vrecpe, vrecps, fmul have inverse throughputs of 1, 0.25, 0.25
        // respectively, while fdiv has inverse throughput of 1.
        printf("[SKIP] Apple M1 chips have division performance roughly on par with the reciprocal instruction\n");
        return 0;
    }

    Func slow, fast;
    Var x;
    Param<float> p(1.0f);

    const int N = 10000000;

    // Compute the golden mean using a continued fraction.
    RDom r(0, N);
    slow(x) = 1.0f;
    fast(x) = 1.0f;
    slow(x) = p / (slow(x) + 1) + 0 * r;
    fast(x) = fast_inverse((fast(x) + 1) + 0 * r);

    // Use wide vectors to ensure we're throughput-limited rather than latency-limited.
    const int vec = 32;

    slow.update().vectorize(x, vec);
    fast.update().vectorize(x, vec);

    slow.compile_jit();
    fast.compile_jit();

    Buffer<float> out_fast(vec), out_slow(vec);

    double slow_time = benchmark([&]() { slow.realize(out_slow); });
    double fast_time = benchmark([&]() { fast.realize(out_fast); });

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
