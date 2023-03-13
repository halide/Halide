#include "Halide.h"
#include <algorithm>
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    // Compute the variance of a 3x3 patch about each pixel
    RDom r(-1, 3, -1, 3);

    // Test a complex summation
    Func input;
    Var x, y, z;
    input(x, y) = cast<float>(x * y + 1);

    Func local_variance;
    Expr input_val = input(x + r.x, y + r.y);
    Expr local_mean = sum(input_val) / 9.0f;
    local_variance(x, y) = sum(input_val * input_val) / 81.0f - local_mean * local_mean;

    Buffer<float> result = local_variance.realize({10, 10});

    for (int y = 0; y < 10; y++) {
        for (int x = 0; x < 10; x++) {
            float local_mean = 0;
            float local_variance = 0;
            for (int rx = -1; rx < 2; rx++) {
                for (int ry = -1; ry < 2; ry++) {
                    float val = (x + rx) * (y + ry) + 1.0f;
                    local_mean += val;
                    local_variance += val * val;
                }
            }
            local_mean /= 9.0f;
            float correct = local_variance / 81.0f - local_mean * local_mean;
            float r = result(x, y);
            float delta = correct - r;
            if (delta < -0.001 || delta > 0.001) {
                printf("result(%d, %d) was %f instead of %f\n", x, y, r, correct);
                return 1;
            }
        }
    }

    // Test the other reductions.
    Func local_product, local_max, local_min;
    local_product(x, y) = product(input_val);
    local_max(x, y) = maximum(input_val);
    local_min(x, y) = minimum(input_val);

    // Try a separable form of minimum too, so we test two reductions
    // in one pipeline. Use a user-provided Func for one of them and
    // unroll the reduction domain.
    Func min_x, min_y;
    RDom kx(-1, 3), ky(-1, 3);
    Func min_y_inner;
    min_x(x, y) = minimum(input(x + kx, y));
    min_y(x, y) = minimum(min_x(x, y + ky), min_y_inner);

    // Vectorize them all, to make life more interesting.
    local_product.vectorize(x, 4);
    local_max.vectorize(x, 4);
    local_min.vectorize(x, 4);
    min_y.vectorize(x, 4);

    // This would fail if the provided Func went unused.
    min_y_inner.update().unroll(ky);

    Buffer<float> prod_im = local_product.realize({10, 10});
    Buffer<float> max_im = local_max.realize({10, 10});
    Buffer<float> min_im = local_min.realize({10, 10});
    Buffer<float> min_im_separable = min_y.realize({10, 10});

    for (int y = 0; y < 10; y++) {
        for (int x = 0; x < 10; x++) {
            float correct_prod = 1.0f;
            float correct_min = 1e10f;
            float correct_max = -1e10f;
            for (int rx = -1; rx < 2; rx++) {
                for (int ry = -1; ry < 2; ry++) {
                    float val = (x + rx) * (y + ry) + 1.0f;
                    correct_prod *= val;
                    correct_min = std::min(correct_min, val);
                    correct_max = std::max(correct_max, val);
                }
            }

            float delta;
            delta = (correct_prod + 10) / (prod_im(x, y) + 10);
            if (delta < 0.99 || delta > 1.01) {
                printf("prod_im(%d, %d) = %f instead of %f\n", x, y, prod_im(x, y), correct_prod);
                return 1;
            }

            delta = correct_min - min_im(x, y);
            if (delta < -0.001 || delta > 0.001) {
                printf("min_im(%d, %d) = %f instead of %f\n", x, y, min_im(x, y), correct_min);
                return 1;
            }

            delta = correct_min - min_im_separable(x, y);
            if (delta < -0.001 || delta > 0.001) {
                printf("min_im(%d, %d) = %f instead of %f\n", x, y, min_im_separable(x, y), correct_min);
                return 1;
            }

            delta = correct_max - max_im(x, y);
            if (delta < -0.001 || delta > 0.001) {
                printf("max_im(%d, %d) = %f instead of %f\n", x, y, max_im(x, y), correct_max);
                return 1;
            }
        }
    }

    // Verify that all inline reductions compile with implicit argument syntax.
    Buffer<float> input_3d = lambda(x, y, z, x * 100.0f + y * 10.0f + ((z + 5 % 10))).realize({10, 10, 10});
    RDom all_z(input_3d.min(2), input_3d.extent(2));

    Func sum_implicit_inner, sum_implicit;
    sum_implicit(_) = sum(input_3d(_, all_z), sum_implicit_inner);
    Buffer<float> sum_implicit_im = sum_implicit.realize({10, 10});

    // The inner Func ends with with _0, _1, etc as its free vars.
    auto args = sum_implicit_inner.args();
    if (args.size() != 2 ||
        args[0].name() != Var(_0).name() ||
        args[1].name() != Var(_1).name()) {
        printf("sum_implicit_inner has the wrong args\n");
        return 1;
    }

    Func product_implicit;
    product_implicit(_) = product(input_3d(_, all_z));
    Buffer<float> product_implicit_im = product_implicit.realize({10, 10});

    Func min_implicit;
    min_implicit(_) = minimum(input_3d(_, all_z));
    Buffer<float> min_implicit_im = min_implicit.realize({10, 10});

    Func max_implicit;
    max_implicit(_, y) = maximum(input_3d(_, y, all_z));
    Buffer<float> max_implicit_im = max_implicit.realize({10, 10});

    Func argmin_implicit;
    argmin_implicit(_) = argmin(input_3d(_, all_z))[0];
    Buffer<int32_t> argmin_implicit_im = argmin_implicit.realize({10, 10});

    Func argmax_implicit;
    argmax_implicit(x, _) = argmax(input_3d(x, _, all_z))[0];
    Buffer<int32_t> argmax_implicit_im = argmax_implicit.realize({10, 10});

    // Verify that the min of negative floats and doubles is correct
    // (this used to be buggy due to the minimum float being the
    // smallest positive float instead of the smallest float).
    float result_f32 = evaluate<float>(minimum(RDom(0, 11) * -0.5f));
    if (result_f32 != -5.0f) {
        printf("minimum is %f instead of -5.0f\n", result_f32);
        return 1;
    }

    double result_f64 = evaluate<double>(minimum(RDom(0, 11) * cast<double>(-0.5f)));
    if (result_f64 != -5.0) {
        printf("minimum is %f instead of -5.0\n", result_f64);
        return 1;
    }

    // Check that min of a bunch of infinities is infinity.
    // Be sure to use strict_float() so that LLVM doesn't optimize away
    // the infinities.
    const float inf_f32 = std::numeric_limits<float>::infinity();
    const double inf_f64 = std::numeric_limits<double>::infinity();
    result_f32 = evaluate<float>(minimum(strict_float(RDom(1, 10) * inf_f32)));
    if (result_f32 != inf_f32) {
        printf("minimum is %f instead of infinity\n", result_f32);
        return 1;
    }
    result_f64 = evaluate<double>(minimum(strict_float(RDom(1, 10) * Expr(inf_f64))));
    if (result_f64 != inf_f64) {
        printf("minimum is %f instead of infinity\n", result_f64);
        return 1;
    }
    result_f32 = evaluate<float>(maximum(strict_float(RDom(1, 10) * -inf_f32)));
    if (result_f32 != -inf_f32) {
        printf("maximum is %f instead of -infinity\n", result_f32);
        return 1;
    }
    result_f64 = evaluate<double>(maximum(strict_float(RDom(1, 10) * Expr(-inf_f64))));
    if (result_f64 != -inf_f64) {
        printf("maximum is %f instead of -infinity\n", result_f64);
        return 1;
    }

    printf("Success!\n");
    return 0;
}
