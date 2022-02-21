#include <cmath>
#include <cstdio>

#include "Halide.h"
using namespace Halide;

int main() {
    ImageParam xyz{Float(32), 3, "xyz"};
    Target t = get_jit_target_from_environment().with_feature(Target::StrictFloat);

    Var col{"col"}, row{"row"};
    Func nan_or_one{"nan_or_one"};
    nan_or_one(col, row) = Halide::select(is_nan(xyz(col, row, 0)), NAN, 1.0f);

    Buffer<float> true_buf{1, 1, 1};
    true_buf(0, 0, 0) = NAN;

    Buffer<float> false_buf{1, 1, 1};
    false_buf(0, 0, 0) = 2.0f;

    Buffer<float> true_result{1, 1};
    Buffer<float> false_result{1, 1};

    xyz.set(true_buf);
    nan_or_one.realize({true_result}, t);

    xyz.set(false_buf);
    nan_or_one.realize({false_result}, t);

    if (std::isnan(true_result(0, 0)) && false_result(0, 0) == 1.0f) {
        printf("Success!\n");
        return 0;
    } else {
        fprintf(stderr, "ERROR: T = %f ; TR = %f ; F = %f ; FR = %f\n",
                true_buf(0, 0, 0), true_result(0, 0), false_buf(0, 0, 0), false_result(0, 0));
        return -1;
    }
}
