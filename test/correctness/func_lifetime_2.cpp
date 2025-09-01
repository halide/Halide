#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

namespace {
const Target target{get_jit_target_from_environment()};
Var x{"x"}, y{"y"}, xi{"xi"}, yi{"yi"};

void validate(const Buffer<int> &im, int add) {
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

TEST(FuncLifetimeTest2, Basic) {
    Func g("g");

    {
        Func f("f");

        f(x, y) = x * y + 1;
        schedule(f);

        {
            SCOPED_TRACE("Realizing function f");
            Buffer<int> imf = f.realize({32, 32});
            validate(imf, 1);
        }

        g(x, y) = x * y + 2;
        schedule(g);

        SCOPED_TRACE("Realizing function g (f still live)");
        Buffer<int> img1 = g.realize({32, 32});
        validate(img1, 2);
    }

    SCOPED_TRACE("Realizing function g (after f destroyed)");
    Buffer<int> img2 = g.realize({32, 32});
    validate(img2, 2);
}
