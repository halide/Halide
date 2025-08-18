#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

TEST(ChunkSharingTest, Basic) {
    Var x("x"), y("y"), i("i"), j("j");
    Func a("a"), b("b"), c("c"), d("d");

    a(i, j) = i + j;
    b(i, j) = a(i, j) + 1;
    c(i, j) = a(i, j) * 2;
    d(x, y) = b(x, y) + c(x, y);

    c.compute_at(d, y);
    b.compute_at(d, y);
    a.compute_at(d, y);

    Buffer<int> im = d.realize({32, 32});

    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 32; x++) {
            int a = x + y;
            int b = a + 1;
            int c = a * 2;
            int d = b + c;
            ASSERT_EQ(im(x, y), d) << "im(" << x << ", " << y << ") = " << im(x, y) << " instead of " << d;
        }
    }
}
