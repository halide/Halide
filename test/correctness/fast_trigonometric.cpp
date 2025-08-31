#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

#ifndef M_PI
#define M_PI 3.14159265358979310000
#endif

TEST(FastTrigonometricTest, Basic) {
    Func sin_f, cos_f;
    Var x;
    Expr t = x / 1000.f;
    const float two_pi = 2.0f * static_cast<float>(M_PI);
    sin_f(x) = fast_sin(-two_pi * t + (1 - t) * two_pi);
    cos_f(x) = fast_cos(-two_pi * t + (1 - t) * two_pi);
    sin_f.vectorize(x, 8);
    cos_f.vectorize(x, 8);

    Buffer<float> sin_result = sin_f.realize({1000});
    Buffer<float> cos_result = cos_f.realize({1000});

    for (int i = 0; i < 1000; ++i) {
        const float alpha = i / 1000.f;
        const float xx = -two_pi * alpha + (1 - alpha) * two_pi;
        EXPECT_NEAR(sin_result(i), sin(xx), 1e-5);
        EXPECT_NEAR(cos_result(i), cos(xx), 1e-5);
    }
}
