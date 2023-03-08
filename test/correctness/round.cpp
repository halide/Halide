#include "Halide.h"
#include <stdio.h>

using namespace Halide;

Var x;

template<class T>
bool test(Expr e, const char *funcname, int vector_width, int N, Buffer<T> &input, T *result) {
    Func f;
    f(x) = e;
    Target t = get_jit_target_from_environment();
    if (t.has_gpu_feature()) {
        if (!t.supports_type(e.type())) {
            printf("(Target does not support (%s x %d), skipping...)\n", type_of<T>() == Float(32) ? "float" : "double", vector_width);
            return true;
        }
        f.gpu_single_thread();
    } else if (vector_width > 1) {
        f.vectorize(x, vector_width);
    }

    Buffer<T> im = f.realize({N});

    printf("Testing %s (%s x %d)\n", funcname, type_of<T>() == Float(32) ? "float" : "double", vector_width);
    bool ok = true;
    for (int i = 0; i < N; i++) {
        if (result[i] != im(i)) {
            printf("Error: %s(%.9g)=%.9g, should be %.9g\n", funcname, input(i), im(i), result[i]);
            ok = false;
        }
    }
    return ok;
}

template<class T>
bool test(Expr e, const char *funcname, int N, Buffer<T> &input, T *result) {
    return test(e, funcname, 1, N, input, result) &&
           test(e, funcname, 2, N, input, result) &&
           test(e, funcname, 4, N, input, result) &&
           test(e, funcname, 8, N, input, result);
}

int main(int argc, char **argv) {
    bool ok = true;
    {
        const int N = 22;
        float inputdata[N] = {
            -2.6f,
            -2.5f,
            -2.3f,
            -1.5f,
            -1.0f,
            -0.5f,
            -0.49999997f,
            -0.2f,
            -0.0f,
            +2.6f,
            +2.5f,
            +2.3f,
            +1.5f,
            +1.0f,
            +0.5f,
            0.49999997f,
            +0.2f,
            +0.0f,
            8388609,
            -8388609,
            16777216,
            -16777218,
        };
        float round_result[N] = {
            -3.0f, -2.0f, -2.0f, -2.0f, -1.0f, -0.0f, -0.0f, -0.0f, -0.0f,
            +3.0f, +2.0f, +2.0f, +2.0f, +1.0f, +0.0f, 0.0f, +0.0f, +0.0f,
            8388609, -8388609, 16777216, -16777218};
        float floor_result[N] = {
            -3.0f, -3.0f, -3.0f, -2.0f, -1.0f, -1.0f, -1.0f, -1.0f, -0.0f,
            +2.0f, +2.0f, +2.0f, +1.0f, +1.0f, +0.0f, 0.0f, +0.0f, +0.0f,
            8388609, -8388609, 16777216, -16777218};
        float ceil_result[N] = {
            -2.0f,
            -2.0f,
            -2.0f,
            -1.0f,
            -1.0f,
            -0.0f,
            -0.0f,
            -0.0f,
            -0.0f,
            +3.0f,
            +3.0f,
            +3.0f,
            +2.0f,
            +1.0f,
            +1.0f,
            1.0f,
            +1.0f,
            +0.0f,
            8388609,
            -8388609,
            16777216,
            -16777218,
        };
        float trunc_result[N] = {
            -2.0f,
            -2.0f,
            -2.0f,
            -1.0f,
            -1.0f,
            -0.0f,
            -0.0f,
            -0.0f,
            -0.0f,
            +2.0f,
            +2.0f,
            +2.0f,
            +1.0f,
            +1.0f,
            +0.0f,
            0.0f,
            +0.0f,
            +0.0f,
            8388609,
            -8388609,
            16777216,
            -16777218,
        };

        Buffer<float> input(N);
        for (int i = 0; i < N; i++) {
            input(i) = inputdata[i];
        }
        ok = ok && test(round(input(x)), "round", N, input, round_result);
        ok = ok && test(floor(input(x)), "floor", N, input, floor_result);
        ok = ok && test(ceil(input(x)), "ceil", N, input, ceil_result);
        ok = ok && test(trunc(input(x)), "trunc", N, input, trunc_result);
    }
    {
        const int N = 24;
        double inputdata[N] = {
            -2.6, -2.5, -2.3, -1.5, -1.0, -0.5, -0.49999999999999994, -0.2, -0.0,
            +2.6, +2.5, +2.3, +1.5, +1.0, +0.5, 0.49999999999999994, +0.2, +0.0,
            8388609, -8388610, 16777216, -16777218,
            4503599627370497, -4503599627370497};
        double round_result[N] = {
            -3.0, -2.0, -2.0, -2.0, -1.0, -0.0, -0.0, -0.0, -0.0,
            +3.0, +2.0, +2.0, +2.0, +1.0, +0.0, 0.0, +0.0, +0.0,
            8388609, -8388610, 16777216, -16777218,
            4503599627370497, -4503599627370497};
        double floor_result[N] = {
            -3.0, -3.0, -3.0, -2.0, -1.0, -1.0, -1.0, -1.0, -0.0,
            +2.0, +2.0, +2.0, +1.0, +1.0, +0.0, 0.0, +0.0, +0.0,
            8388609, -8388610, 16777216, -16777218,
            4503599627370497, -4503599627370497};
        double ceil_result[N] = {
            -2.0, -2.0, -2.0, -1.0, -1.0, -0.0, -0.0, -0.0, -0.0,
            +3.0, +3.0, +3.0, +2.0, +1.0, +1.0, 1.0, +1.0, +0.0,
            8388609, -8388610, 16777216, -16777218,
            4503599627370497, -4503599627370497};
        double trunc_result[N] = {
            -2.0, -2.0, -2.0, -1.0, -1.0, -0.0, -0.0, -0.0, -0.0,
            +2.0, +2.0, +2.0, +1.0, +1.0, +0.0, 0.0, +0.0, +0.0,
            8388609, -8388610, 16777216, -16777218,
            4503599627370497, -4503599627370497};
        Buffer<double> input(N);
        for (int i = 0; i < N; i++) {
            input(i) = inputdata[i];
        }
        ok = ok && test(round(input(x)), "round", N, input, round_result);
        ok = ok && test(floor(input(x)), "floor", N, input, floor_result);
        ok = ok && test(ceil(input(x)), "ceil", N, input, ceil_result);
        ok = ok && test(trunc(input(x)), "trunc", N, input, trunc_result);
    }
    if (ok) {
        printf("Success!\n");
    }
    return ok ? 0 : -1;
}
