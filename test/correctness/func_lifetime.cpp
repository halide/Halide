#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

namespace {
const Target target{get_jit_target_from_environment()};
Var x{"x"}, y{"y"}, xi{"xi"}, yi{"yi"};

void expect_valid(const Buffer<int> &im, const int add) {
    for (int i = 0; i < im.width(); i++) {
        for (int j = 0; j < im.height(); j++) {
            int correct = i * j + add;
            EXPECT_EQ(im(i, j), correct) << "i = " << i << ", j = " << j;
        }
    }
}

void schedule(Func &f) {
    if (target.has_gpu_feature()) {
        f.gpu_tile(x, y, xi, yi, 8, 8);
    } else if (target.has_feature(Target::HVX)) {
        f.hexagon().vectorize(x, 32);
    }
}
}  // namespace

TEST(FuncLifetimeTest, Basic) {
    Func f("f");

    f(x, y) = x * y + 1;
    schedule(f);

    {
        SCOPED_TRACE("Initial realization of Func f");
        const Buffer<int> imf = f.realize({32, 32});
        expect_valid(imf, 1);
    }

    {
        SCOPED_TRACE("Create (and destroy) a new function g.");
        Func g("g");

        g(x, y) = x * y + 2;
        schedule(g);

        const Buffer<int> img = g.realize({32, 32});
        expect_valid(img, 2);
    }

    {
        SCOPED_TRACE("Try using f again to ensure it is still valid (after g's destruction)");
        const Buffer<int> imf2 = f.realize({32, 32});
        expect_valid(imf2, 1);
    }
}
