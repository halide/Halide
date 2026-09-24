#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("shift_inwards_and_blend_race", "Tail strategy ShiftInwardsAndBlend may not be used to split x because other vars stemming from the same original Var or RVar are marked as parallel.", []() {
        Func f;
        Var x;

        f(x) = 0;
        f(x) += 4;

        // This schedule should be forbidden, because it causes a race condition.
        f.update().vectorize(x, 8, TailStrategy::ShiftInwardsAndBlend).parallel(x);
    });
}
