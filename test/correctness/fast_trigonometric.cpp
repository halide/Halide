#include "Halide.h"

#ifndef M_PI
#define M_PI 3.14159265358979310000
#endif

using namespace Halide;

int main(int argc, char **argv) {
    Func sin_f, cos_f;
    Var x;
    Expr t = x / 1000.f;
    const float two_pi = 2.0f * static_cast<float>(M_PI);
    sin_f(x) = fast_sin(-two_pi * t + (1-t) * two_pi);
    cos_f(x) = fast_cos(-two_pi * t + (1-t) * two_pi);
    sin_f.vectorize(x, 8);
    cos_f.vectorize(x, 8);

    Buffer<float> sin_result = sin_f.realize(1000);
    Buffer<float> cos_result = cos_f.realize(1000);

    for (int i = 0; i < 1000; ++i) {
        const float alpha = i / 1000.f;
        const float x = -two_pi * alpha + (1 - alpha) * two_pi;
        const float sin_x = sin_result(i);
        const float cos_x = cos_result(i);
        const float sin_x_ref = sin(x);
        const float cos_x_ref = cos(x);
        if (std::abs(sin_x_ref - sin_x) > 1e-5) {
            fprintf(stderr, "fast_sin(%.6f) = %.20f not equal to %.20f\n", x, sin_x, sin_x_ref);
            exit(1);
        }
        if (std::abs(cos_x_ref - cos_x) > 1e-5) {
            fprintf(stderr, "fast_cos(%.6f) = %.20f not equal to %.20f\n", x, cos_x, cos_x_ref);
            exit(1);
        }
    }
    printf("Success!\n");
    return 0;
}
