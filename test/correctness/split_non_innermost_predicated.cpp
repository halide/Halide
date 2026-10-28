#include "Halide.h"
#include "expect_user_error.h"

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("split_non_innermost_predicated", "Cannot split a loop variable resulting from a split using PredicateLoads or PredicateStores.", []() {
        Func f;
        Var x;
        f(x) = x;
        Var xo, xi, xio, xii;
        // We don't support predicated splits that aren't the innermost loop.
        f.compute_root().split(x, xo, xi, 8, TailStrategy::PredicateStores).split(xi, xio, xii, 9);

        Func g;
        g(x) = f(x);
        g.realize({10});
    });
}
