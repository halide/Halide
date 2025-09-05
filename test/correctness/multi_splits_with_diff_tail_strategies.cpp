#include "Halide.h"
#include "check_call_graphs.h"
#include <gtest/gtest.h>

using namespace Halide;

TEST(MultiSplitsWithDiffTailStrategiesTest, Basic) {
    // ApplySplit should respect the order of the application of substitutions/
    // predicates/lets; otherwise, this combination of tail strategies will
    // cause an access out of bound error.
    for (TailStrategy tail_strategy : {TailStrategy::GuardWithIf, TailStrategy::Predicate, TailStrategy::PredicateLoads}) {
        Func f("f"), input("input");
        Var x("x"), y("y"), c("c");

        f(x, y, c) = x + y + c;

        f.reorder(c, x, y);
        Var yo("yo"), yi("yi");
        f.split(y, yo, yi, 2, TailStrategy::RoundUp);

        Var yoo("yoo"), yoi("yoi");
        f.split(yo, yoo, yoi, 64, tail_strategy);

        Buffer<int> im = f.realize({3000, 2000, 3});
        auto func = [](int x, int y, int c) {
            return x + y + c;
        };
        EXPECT_EQ(check_image(im, func), 0);
    }
}
