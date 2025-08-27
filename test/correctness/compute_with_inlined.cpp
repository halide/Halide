#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

namespace {
Func inlined(Func in) {
    Var x("x"), y("y");

    Func f("f");
    f(x, y) = in(x, y) >> 2;

    Func g("g");
    g(x, y) = f(x, y) >> 2;
    return g;
}
}  // anonymous namespace

TEST(ComputeWithInlinedTest, Basic) {
    Func one("one"), three("three");
    Var x("x"), y("y");

    one(x, y) = x + y;

    const int num_stages = 4;
    Func two[num_stages];
    two[0](x, y) = one(x, y);
    for (int j = 1; j < num_stages; j++) {
        two[j](x, y) = inlined(one)(x, y) + j;
    }

    Expr temp = 0;
    for (int j = 0; j < num_stages; j++) {
        temp += two[j](x, y) / num_stages;
    }
    three(x, y) = temp;

    two[0].compute_root();
    for (int i = 1; i < num_stages; i++) {
        // These functions refer to other functions that were scheduled inlined
        // ("f" and "g"), which used to cause compilation error due to
        // incorrect realization order.
        two[i].compute_root().compute_with(two[0], Var::outermost());
    }
    one.compute_at(two[0], Var::outermost());
    three.compute_root();

    three.realize({1024, 1024});
}
