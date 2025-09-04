#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

TEST(HelloGpu, BasicGpuComputation) {
    Var x("x"), y("y"), xi("xi"), yi("yi");
    Func f("f");

    f(x, y) = x * y + 2.4f;

    Target target = get_jit_target_from_environment();
    if (target.has_gpu_feature()) {
        f.gpu_tile(x, y, xi, yi, 8, 8);
    }

    Buffer<float> imf = f.realize({32, 32}, target);

    // Check the result was what we expected
    for (int i = 0; i < 32; i++) {
        for (int j = 0; j < 32; j++) {
            float correct = i * j + 2.4f;
            ASSERT_NEAR(imf(i, j), correct, 0.001f) << "at (" << i << ", " << j << ")";
        }
    }
}
