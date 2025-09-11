#include "Halide.h"
#include "check_call_graphs.h"
#include <gtest/gtest.h>

using namespace Halide;

namespace {
class VectorizeNestedTest : public ::testing::Test {};
}  // namespace

TEST_F(VectorizeNestedTest, Vectorize2dRoundUp) {
    const int width = 32;
    const int height = 24;

    Func f("f");
    Var x("x"), y("y"), xi("xi"), yi("yi");

    f(x, y) = 3 * x + y;

    f.compute_root()
        .tile(x, y, x, y, xi, yi, 8, 4, TailStrategy::RoundUp)
        .vectorize(xi)
        .vectorize(yi);

    Buffer<int> result = f.realize({width, height});

    auto cmp_func = [](int x, int y) {
        return 3 * x + y;
    };
    EXPECT_EQ(check_image(result, cmp_func), 0);
}

TEST_F(VectorizeNestedTest, Vectorize2dGuardWithIfAndPredicate) {
    for (TailStrategy tail_strategy : {TailStrategy::GuardWithIf, TailStrategy::Predicate}) {
        const int width = 33;
        const int height = 22;

        Func f("f");
        Var x("x"), y("y"), xi("xi"), yi("yi");

        f(x, y) = 3 * x + y;

        f.compute_root()
            .tile(x, y, x, y, xi, yi, 8, 4, tail_strategy)
            .vectorize(xi)
            .vectorize(yi);

        Buffer<int> result = f.realize({width, height});

        auto cmp_func = [](int x, int y) {
            return 3 * x + y;
        };
        EXPECT_EQ(check_image(result, cmp_func), 0);
    }
}

TEST_F(VectorizeNestedTest, Vectorize2dInlinedWithUpdate) {
    const int width = 33;
    const int height = 22;

    Func f, inlined;
    Var x("x"), y("y"), xi("xi"), yi("yi");
    RDom r(0, 10, "r");
    inlined(x) = x;
    inlined(x) += r;
    f(x, y) = inlined(x) + 2 * y;

    f.compute_root()
        .tile(x, y, x, y, xi, yi, 8, 4, TailStrategy::GuardWithIf)
        .vectorize(xi)
        .vectorize(yi);

    Buffer<int> result = f.realize({width, height});

    auto cmp_func = [](int x, int y) {
        return x + 2 * y + 45;
    };
    EXPECT_EQ(check_image(result, cmp_func), 0);
}

TEST_F(VectorizeNestedTest, Vectorize2dWithInnerFor) {
    const int width = 33;
    const int height = 22;

    Func f;
    Var x("x"), y("y"), c("c"), xi("xi"), yi("yi");
    f(x, y, c) = 3 * x + y + 7 * c;

    f.compute_root()
        .tile(x, y, x, y, xi, yi, 8, 4, TailStrategy::GuardWithIf)
        .reorder(c, xi, yi, x, y)
        .vectorize(xi)
        .vectorize(yi);

    Buffer<int> result = f.realize({width, height, 3});

    auto cmp_func = [](int x, int y, int c) {
        return 3 * x + y + 7 * c;
    };
    EXPECT_EQ(check_image(result, cmp_func), 0);
}

TEST_F(VectorizeNestedTest, Vectorize2dWithComputeAtVectorized) {
    const int width = 16;
    const int height = 16;

    Func f("f"), g("g");
    Var x("x"), y("y");
    f(x, y) = 3 * x + y;
    g(x, y) = f(x, y) + f(x + 1, y);

    Var xi("xi");
    g.split(x, x, xi, 8).vectorize(xi);
    f.compute_at(g, xi).vectorize(x);

    Buffer<int> result = g.realize({width, height});

    auto cmp_func = [](int x, int y) {
        return 6 * x + 3 + 2 * y;
    };
    EXPECT_EQ(check_image(result, cmp_func), 0);
}

TEST_F(VectorizeNestedTest, Vectorize2dWithComputeAt) {
    const int width = 35;
    const int height = 17;

    Func f("f"), g("g");
    Var x("x"), y("y");
    f(x, y) = 3 * x + y;
    g(x, y) = f(x, y) + f(x + 1, y);

    Var xi("xi"), xii("xii");
    g.split(x, x, xi, 8, TailStrategy::GuardWithIf)
        .split(xi, xi, xii, 2, TailStrategy::GuardWithIf)
        .vectorize(xi)
        .vectorize(xii);
    f.compute_at(g, xii).vectorize(x);

    Buffer<int> result = g.realize({width, height});

    auto cmp_func = [](int x, int y) {
        return 6 * x + 3 + 2 * y;
    };
    EXPECT_EQ(check_image(result, cmp_func), 0);
}

TEST_F(VectorizeNestedTest, VectorizeAllD) {
    const int width = 12;
    const int height = 10;

    Func f("f");
    Var x("x"), y("y"), xi("xi"), yi("yi");

    f(x, y) = 3 * x + y;

    f.compute_root()
        .tile(x, y, x, y, xi, yi, 4, 2, TailStrategy::GuardWithIf)
        .vectorize(x)
        .vectorize(y)
        .vectorize(xi)
        .vectorize(yi);

    f.bound(x, 0, width).bound(y, 0, height);
    Buffer<int> result = f.realize({width, height});

    auto cmp_func = [](int x, int y) {
        return 3 * x + y;
    };
    EXPECT_EQ(check_image(result, cmp_func), 0);
}

TEST_F(VectorizeNestedTest, VectorizeLetsOrder) {
    const int width = 128;
    const int height = 128;

    Var x("x"), y("y"), yo("yo"), yi("yi"), yoi("yoi"), yoio("yoio"), yoii("yoii");
    Func f("f");
    f(x, y) = x + y;
    f.split(y, yo, yi, 8, TailStrategy::Auto)
        .split(yo, yo, yoi, 4, TailStrategy::RoundUp)
        .vectorize(yoi)
        .vectorize(yi)
        .split(yoi, yoio, yoii, 2, TailStrategy::Auto);
    Buffer<int> result = f.realize({width, height});

    auto cmp_func = [](int x, int y) {
        return x + y;
    };
    EXPECT_EQ(check_image(result, cmp_func), 0);
}
TEST_F(VectorizeNestedTest, VectorizeInnerOfScalarization) {
    ImageParam in(UInt(8), 2);

    Var x("x_inner"), y("y_inner");

    Func out;
    out(x, y) = in(x, y);

    Var xo("xo"), yo("yo");
    out.split(x, xo, x, 8, TailStrategy::RoundUp)
        .split(y, yo, y, 8, TailStrategy::GuardWithIf)
        .vectorize(x)
        .vectorize(y);

    // We are looking for a specific loop, which shouldn't have been scalarized.
    struct CheckForScalarizedLoop : Internal::IRMutator {
        using IRMutator::visit;

        Internal::Stmt visit(const Internal::For *op) override {
            if (Internal::ends_with(op->name, ".x_inner")) {
                x_loop_found = true;
            }

            if (Internal::ends_with(op->name, ".y_inner")) {
                y_loop_found = true;
            }

            return IRMutator::visit(op);
        }
        bool x_loop_found = false;
        bool y_loop_found = false;
    } checker;
    out.add_custom_lowering_pass(&checker, nullptr);

    out.compile_jit();

    EXPECT_FALSE(checker.x_loop_found) << "Found scalarized loop for " << x;
    EXPECT_TRUE(checker.y_loop_found) << "Expected to find scalarized loop for " << y;
}
