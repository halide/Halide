#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

TEST(ParallelGpuNestedTest, ParallelGpuNested) {
    Var x, y, z;
    Func f;

    Param<int> k;
    k.set(3);

    f(x, y, z) = x * y + z * k + 1;

    Target t = get_jit_target_from_environment();
    if (t.has_gpu_feature()) {
        Var xi, yi;
        f.gpu_tile(x, y, xi, yi, 16, 16);
    } else if (t.has_feature(Target::HVX)) {
        f.hexagon(y);
    } else {
        GTEST_SKIP() << "No GPU target enabled.";
    }
    f.parallel(z);

    Buffer<int> im;
    ASSERT_NO_THROW(im = f.realize({64, 64, 64}));

    for (int x = 0; x < 64; x++) {
        for (int y = 0; y < 64; y++) {
            for (int z = 0; z < 64; z++) {
                EXPECT_EQ(im(x, y, z), x * y + z * 3 + 1) 
                    << "im(" << x << ", " << y << ", " << z << ")";
            }
        }
    }
}
