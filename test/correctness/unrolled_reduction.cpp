#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

TEST(UnrolledReductionTest, Basic) {
    Var x("x"), y("y"), z("z");

    Buffer<float> noise(32);
    for (int i = 0; i < 32; i++) {
        noise(i) = (float)rand() / (float)RAND_MAX;
    }

    Func f("f");
    Func g("g");
    RDom r(0, 32);

    g(x, y) = 0.0f;
    g(r.x, y) += noise(r.x);

    f(x, y, z) = g(x, y) + g(x + 1, y);

    RVar rxo, rxi;
    g.compute_at(f, y).update().split(r.x, rxo, rxi, 2).unroll(rxi);
    f.unroll(z, 2);

    ASSERT_NO_THROW(f.realize({64, 64, 4}));
}
