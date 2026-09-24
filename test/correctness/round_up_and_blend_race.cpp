#include "Halide.h"
#include "expect_user_error.h"

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("round_up_and_blend_race", "Tail strategy RoundUpAndBlend may not be used to split x.xi because other vars stemming from the same original Var or RVar are marked as parallel.", []() {
        Func f;
        Var x;

        f(x) = 0;
        f(x) += 4;

        // This schedule should be forbidden, because it causes a race condition.
        Var xo, xi;
        f.update()
            .split(x, xo, xi, 8, TailStrategy::RoundUp)
            .vectorize(xi, 16, TailStrategy::RoundUpAndBlend)  // Access beyond the end of each slice
            .parallel(xo);
    });
}
