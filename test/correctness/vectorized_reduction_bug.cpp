#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

TEST(VectorizedReductionBugTest, VectorizedSelect) {
    Func sum("sum"), foo("foo");
    Var x("x"), y("y"), c("c");

    RDom r(1, 2, "r");

    // sum(x, y) should equal 3
    sum(x, y) += r.x;

    foo(x, y, c) = select(c == 3, 255, sum(x, y));
    // foo(x, y, c) should equal (3, 3, 3, 255);

    foo.vectorize(c, 4);

    Buffer<int32_t> output;
    ASSERT_NO_THROW(output = foo.realize({2, 2, 4}));
    for (int y = 0; y < 2; y++) {
        for (int x = 0; x < 2; x++) {
            for (int c = 0; c < 4; c++) {
                int correct = (c == 3 ? 255 : 3);
                EXPECT_EQ(output(x, y, c), correct) << "output(" << x << ", " << y << ", " << c << ")";
            }
        }
    }
}

TEST(VectorizedReductionBugTest, VectorizedReduction) {
    Func f("f"), g("g"), h("h");
    Var x("x"), y("y"), c("c");

    h(x, y) = x + y;
    h.compute_root();

    g(x, y) = 0;
    g(x, 0) = sum(h(x, RDom(0, 120)));

    // Transpose.
    f(y, x) = g(x, y);

    Var x_outer("x_outer");
    f.split(x, x_outer, x, 8 * 2);

    g.compute_at(f, x_outer);
    g.update(0).vectorize(x);

    f.compute_root();
    Buffer<int32_t> im;
    ASSERT_NO_THROW(im = f.realize({100, 100}));

    for (int y = 0; y < im.height(); y++) {
        for (int x = 0; x < im.width(); x++) {
            int correct = (x != 0) ? 0 : 120 * (x + y) + 120 * 119 / 2;
            EXPECT_EQ(im(x, y), correct) << "im(" << x << ", " << y << ")";
        }
    }
}
