#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

TEST(GPUVectorizedSharedMemory, Basic) {
    // This test is only relevant for opencl
    if (!get_jit_target_from_environment().has_feature(Target::OpenCL)) {
        GTEST_SKIP() << "OpenCL not enabled";
    }

    Func f, g, h;
    Var x;

    f(x) = x;
    g(x) = f(x - 1) + f(x + 1);
    RDom r(0, 10);
    g(x) += sum(r);
    h(x) = g(x - 1) + g(x + 1);

    f.compute_root();

    Var xo, xi;
    h.split(x, xo, xi, 16).vectorize(xi, 4).gpu_threads(xi).gpu_blocks(xo);
    g.compute_at(h, xo);
    g.split(x, xo, xi, 4).gpu_threads(xo).vectorize(xi);
    g.update().split(x, xo, xi, 4).gpu_threads(xo).vectorize(xi);

    Buffer<int> out = h.realize({512});

    for (int x = 0; x < out.width(); x++) {
        int correct = 4 * x + 90;
        EXPECT_EQ(out(x), correct) << "at " << x;
    }
}
