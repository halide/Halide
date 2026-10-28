#include "Halide.h"
#include "expect_user_error.h"

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("split_inner_wrong_tail_strategy", "It may redundantly recompute some values, which could change the meaning of the algorithm. Use TailStrategy::GuardWithIf instead.", []() {
        Func f;
        Var x;
        f(x) = x;
        f(x) += 1;
        Var xo, xi, xio, xii;
        // Would redundantly redo some +=1, and create incorrect output.
        f.compute_root();
        f.update().split(x, xo, xi, 8).split(xi, xio, xii, 9, TailStrategy::RoundUp);

        Func g;
        g(x) = f(x);
        g.realize({10});
    });
}
