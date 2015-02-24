#include "Halide.h"
#include <stdio.h>
#include "clock.h"

using namespace Halide;

// 32-bit windows defines powf as a macro, which won't work for us.
#ifdef WIN32
extern "C" __declspec(dllexport) float pow_ref(float x, float y) {
    return pow(x, y);
}
#else
extern "C" float pow_ref(float x, float y) {
    return powf(x, y);
}
#endif

HalideExtern_2(float, pow_ref, float, float);

int main(int argc, char **argv) {
    Func f, g, h;
    Var x, y;

    f(x, y) = pow_ref((x+1)/512.0f, (y+1)/512.0f);
    g(x, y) = pow((x+1)/512.0f, (y+1)/512.0f);
    h(x, y) = fast_pow((x+1)/512.0f, (y+1)/512.0f);
    f.vectorize(x, 8);
    g.vectorize(x, 8);
    h.vectorize(x, 8);

    f.compile_jit();
    g.compile_jit();
    h.compile_jit();

    Image<float> correct_result(2048, 768);
    Image<float> fast_result(2048, 768);
    Image<float> faster_result(2048, 768);

    const int iterations = 20;

    double t1 = current_time();
    for (int i = 0; i < iterations; i++) {
        f.realize(correct_result);
    }
    double t2 = current_time();
    for (int i = 0; i < iterations; i++) {
        g.realize(fast_result);
    }
    double t3 = current_time();
    for (int i = 0; i < iterations; i++) {
        h.realize(faster_result);
    }
    double t4 = current_time();

    RDom r(correct_result);
    Func fast_error, faster_error;
    Expr fast_delta = correct_result(r.x, r.y) - fast_result(r.x, r.y);
    Expr faster_delta = correct_result(r.x, r.y) - faster_result(r.x, r.y);
    fast_error() += cast<double>(fast_delta * fast_delta);
    faster_error() += cast<double>(faster_delta * faster_delta);

    Image<double> fast_err = fast_error.realize();
    Image<double> faster_err = faster_error.realize();

    int N = correct_result.width() * correct_result.height() * iterations;
    fast_err(0) = sqrt(fast_err(0)/N);
    faster_err(0) = sqrt(faster_err(0)/N);

    printf("powf: %f ns per pixel\n"
           "Halide's pow: %f ns per pixel (rms error = %0.10f)\n"
           "Halide's fast_pow: %f ns per pixel (rms error = %0.10f)\n",
           1000000*(t2-t1) / N,
           1000000*(t3-t2) / N, fast_err(0),
           1000000*(t4-t3) / N, faster_err(0));

    if (fast_err(0) > 0.000001) {
        printf("Error for pow too large\n");
        return -1;
    }

    if (faster_err(0) > 0.0001) {
        printf("Error for fast_pow too large\n");
        return -1;
    }

    if (t2-t1 < t3-t2) {
        printf("powf is faster than Halide's pow\n");
        return -1;
    }

    if (t3-t2 < t4-t3) {
        printf("pow is faster than fast_pow\n");
        return -1;
    }

    printf("Success!\n");

    return 0;
}
