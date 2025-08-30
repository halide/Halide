#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

namespace {
float inverse_factorial(int x) {
    double y = 1;
    for (int i = 2; i <= x; i++) {
        y /= i;
    }
    return (float)y;
}
}  // namespace

TEST(IntegerPowersTest, TaylorSeriesAccuracy) {
    Func f;
    Var x;

    // Create x scaled down by 256. We're going to intentionally do
    // something numerically unstable below, so we prevent folding out
    // the /256, or large powers of x will be inf
    Expr xf = strict_float(x / 256.0f);

    {

        // Compute the taylor series approximation for sin
        // x - x^3 / 3! + x^5 / 5! + x^7 / 7! ....

        Expr y1 = 0.0f;
        for (int k = 0; k < 20; k++) {
            y1 += Halide::pow(-1, k) * pow(xf, 1 + 2 * k) * inverse_factorial(1 + 2 * k);
        }

        Func approx_sin_1;
        approx_sin_1(x) = y1;

        // Try a different way to express the Taylor series that should
        // have fewer numerical precision issues. The large inverse
        // factorials in the previous version tend to disappear entirely.
        // x*(1 - x*x/(2*3) * (1 - x*x/(4*5) * (1 - x*x/(6*7) * (1 - x*x/(8*9) * ( ... )))))
        Expr y2 = 1.0f;
        for (int k = 20; k > 0; k--) {
            y2 = 1 - (y2 * pow(xf, 2)) / (2 * k * (2 * k + 1));
        }
        y2 *= xf;

        Func approx_sin_2;
        approx_sin_2(x) = y2;

        Func exact_sin;
        exact_sin(x) = sin(xf);

        // Evaluate from 0 to 5
        Buffer<float> approx_result_1 = approx_sin_1.realize({256 * 5});
        Buffer<float> approx_result_2 = approx_sin_2.realize({256 * 5});
        Buffer<float> exact_result = exact_sin.realize({256 * 5});

        Func rms_1, rms_2;
        RDom r(exact_result);
        rms_1() = sqrt(sum(pow(approx_result_1(r) - exact_result(r), 2), "rms_1_sum"));
        rms_2() = sqrt(sum(pow(approx_result_2(r) - exact_result(r), 2), "rms_2_sum"));
        Buffer<float> error_1 = rms_1.realize();
        Buffer<float> error_2 = rms_2.realize();

        EXPECT_LE(error_1(0), 0.0001f) << "approx_sin_1 RMS error too large: " << error_1(0);
        EXPECT_LE(error_2(0), 0.0001f) << "approx_sin_2 RMS error too large: " << error_2(0);
    }

    {
        xf = xf + 1;

        // Now take negative powers for a spin.
        // exp(1/x) = 1 + 1/x + 1/(2*x^2) + 1/(6*x^3) + ...
        Func approx_exp_1;
        Expr y1 = 0.0f;
        for (int k = 0; k < 20; k++) {
            y1 += pow(xf, -k) * inverse_factorial(k);
        }
        approx_exp_1(x) = y1;

        // A different factorization.
        // exp(1/x) = 1 + (1 + (1 + (1 + ...)/(3x))/(2x))/x;
        Func approx_exp_2;
        Expr y2 = 0.0f;
        for (int k = 20; k > 0; k--) {
            y2 = 1 + y2 / (k * xf);
        }
        approx_exp_2(x) = y2;

        Func exact_exp;
        exact_exp(x) = exp(1.0f / xf);

        // Evaluate from 0 to 5
        Buffer<float> approx_result_1 = approx_exp_1.realize({256 * 5});
        Buffer<float> approx_result_2 = approx_exp_2.realize({256 * 5});
        Buffer<float> exact_result = exact_exp.realize({256 * 5});

        Func rms_1, rms_2;
        RDom r(exact_result);
        rms_1() = sqrt(sum(pow(approx_result_1(r) - exact_result(r), 2), "rms_1_neg_sum"));
        rms_2() = sqrt(sum(pow(approx_result_2(r) - exact_result(r), 2), "rms_2_neg_sum"));
        Buffer<float> error_1 = rms_1.realize();
        Buffer<float> error_2 = rms_2.realize();

        EXPECT_LE(error_1(0), 0.0001f) << "approx_exp_1 RMS error too large: " << error_1(0);
        EXPECT_LE(error_2(0), 0.0001f) << "approx_exp_2 RMS error too large: " << error_2(0);
    }
}
