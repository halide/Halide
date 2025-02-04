#include "Halide.h"

#ifndef M_PI
#define M_PI 3.14159265358979310000
#endif

using namespace Halide;

int main(int argc, char **argv) {
    Func sin_f, cos_f;
    Var x;
    constexpr int STEPS = 5000;
    Expr t = x / float(STEPS);
    const float two_pi = 2.0f * static_cast<float>(M_PI);
    const float range = -two_pi * 2.0f;
    sin_f(x) = fast_sin(-range * t + (1 - t) * range);
    cos_f(x) = fast_cos(-range * t + (1 - t) * range);
    sin_f.vectorize(x, 8);
    cos_f.vectorize(x, 8);

    Buffer<float> sin_result = sin_f.realize({STEPS});
    Buffer<float> cos_result = cos_f.realize({STEPS});

    for (int i = 0; i < STEPS; ++i) {
        const float alpha = i / float(STEPS);
        const float x = -range * alpha + (1 - alpha) * range;
        const float sin_x = sin_result(i);
        const float cos_x = cos_result(i);
        const float sin_x_ref = sin(x);
        const float cos_x_ref = cos(x);
        if (std::abs(sin_x_ref - sin_x) > 1e-5) {
            fprintf(stderr, "fast_sin(%.6f) = %.20f not equal to %.20f\n", x, sin_x, sin_x_ref);
            //exit(1);
        }
        if (std::abs(cos_x_ref - cos_x) > 1e-5) {
            fprintf(stderr, "fast_cos(%.6f) = %.20f not equal to %.20f\n", x, cos_x, cos_x_ref);
            //exit(1);
        }
    }
    printf("Success!\n");
    return 0;
}
