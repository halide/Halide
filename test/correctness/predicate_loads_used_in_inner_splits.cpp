#include "Halide.h"
#include "expect_user_error.h"

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("predicate_loads_used_in_inner_splits", "PredicateLoads may not be used to split a Var stemming from the inner Var of a prior split.", []() {
        Func f;
        Var x, xo, xi, xio, xii;
        f(x) = x;
        f.split(x, xo, xi, 2, TailStrategy::Auto)
            .split(xi, xio, xii, 4, TailStrategy::PredicateLoads)
            .reorder(xo, xio, xii);
    });
}
