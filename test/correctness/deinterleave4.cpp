#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

namespace {
Var x;

Func upsample(Func f) {
    Func u;
    u(x) = f(x / 2 + 1);
    return u;
}

Func build() {
    Func in;
    in(x) = x;
    in.compute_root();

    Func up = upsample(upsample(in));

    return up;
}
}  // namespace

TEST(Deinterleave4Test, Basic) {
    Func f1 = build();
    Func f2 = build();
    f2.bound(x, 0, 64).vectorize(x);

    Buffer<int> o1 = f1.realize({64});
    Buffer<int> o2 = f2.realize({64});

    for (int i = 0; i < o2.width(); i++) {
        EXPECT_EQ(o1(i), o2(i)) << "Mismatch at x=" << i;
    }
}
