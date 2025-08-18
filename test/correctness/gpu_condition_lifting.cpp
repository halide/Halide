#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

TEST(GpuConditionLiftingTest, Basic) {
    // See https://github.com/halide/Halide/issues/4297
    Target target = get_jit_target_from_environment();
    if (!target.has_gpu_feature()) {
        GTEST_SKIP() << "No GPU target enabled.";
    }
    Var x, y, z;
    Func f;
    f(x, y, z) = 0;
    Var yo, yi;
    f.split(y, yo, yi, 32, TailStrategy::GuardWithIf)
        .reorder(x, z, yi, yo)
        .gpu_blocks(yo)
        .gpu_blocks(yi)
        .gpu_blocks(z);

    Buffer<int> imf = f.realize({10, 10, 10}, target);
}
