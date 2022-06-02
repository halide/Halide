#include "Halide.h"
#include "halide_benchmark.h"
#include <algorithm>
#include <cstdio>

using namespace Halide;
using namespace Halide::Tools;

// powf() is a macro in some environments, so always wrap it
extern "C" HALIDE_EXPORT_SYMBOL float pow_ref(float x, float y) {
    return powf(x, y);
}
HalideExtern_2(float, pow_ref, float, float);

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (target.arch == Target::WebAssembly) {
        printf("[SKIP] Performance tests are meaningless and/or misleading under WebAssembly interpreter.\n");
        return 0;
    }

    Func f, g, h;
    Var x, y;

    Param<int> pows_per_pixel;

    RDom s(0, pows_per_pixel);
    f(x, y) = sum(pow_ref((x + 1) / 512.0f, (y + 1 + s) / 512.0f));
    g(x, y) = sum(pow((x + 1) / 512.0f, (y + 1 + s) / 512.0f));
    h(x, y) = sum(fast_pow((x + 1) / 512.0f, (y + 1 + s) / 512.0f));
    f.vectorize(x, 8);
    g.vectorize(x, 8);
    h.vectorize(x, 8);

    Buffer<float> correct_result(2048, 768);
    Buffer<float> fast_result(2048, 768);
    Buffer<float> faster_result(2048, 768);

    pows_per_pixel.set(1);

    f.realize(correct_result);
    g.realize(fast_result);
    h.realize(faster_result);

    pows_per_pixel.set(20);

    // All profiling runs are done into the same buffer, to avoid
    // cache weirdness.
    Buffer<float> timing_scratch(256, 256);
    double t1 = 1e3 * benchmark([&]() { f.realize(timing_scratch); });
    double t2 = 1e3 * benchmark([&]() { g.realize(timing_scratch); });
    double t3 = 1e3 * benchmark([&]() { h.realize(timing_scratch); });

    RDom r(correct_result);
    Func fast_error, faster_error;
    Expr fast_delta = correct_result(r.x, r.y) - fast_result(r.x, r.y);
    Expr faster_delta = correct_result(r.x, r.y) - faster_result(r.x, r.y);
    fast_error() += cast<double>(fast_delta * fast_delta);
    faster_error() += cast<double>(faster_delta * faster_delta);

    Buffer<double> fast_err = fast_error.realize();
    Buffer<double> faster_err = faster_error.realize();

    int timing_N = timing_scratch.width() * timing_scratch.height() * 10;
    int correctness_N = fast_result.width() * fast_result.height();
    fast_err() = sqrt(fast_err() / correctness_N);
    faster_err() = sqrt(faster_err() / correctness_N);

    printf("powf: %f ns per pixel\n"
           "Halide's pow: %f ns per pixel (rms error = %0.10f)\n"
           "Halide's fast_pow: %f ns per pixel (rms error = %0.10f)\n",
           1000000 * t1 / timing_N,
           1000000 * t2 / timing_N, fast_err(),
           1000000 * t3 / timing_N, faster_err());

    if (fast_err() > 0.000001) {
        printf("Error for pow too large\n");
        return -1;
    }

    if (faster_err() > 0.0001) {
        printf("Error for fast_pow too large\n");
        return -1;
    }

    if (t1 < t2) {
        printf("powf is faster than Halide's pow\n");
        return -1;
    }

    if (t2 * 1.5 < t3) {
        printf("pow is more than 1.5x faster than fast_pow\n");
        return -1;
    }

    printf("Success!\n");
    return 0;
}
