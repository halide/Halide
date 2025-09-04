#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

class GPUSpecializeTest : public ::testing::Test {
protected:
    void SetUp() override {
        Halide::Target target = get_jit_target_from_environment();
        if (!target.has_gpu_feature()) {
            GTEST_SKIP() << "No GPU target enabled";
        }
        if (target.has_feature(Target::Vulkan) && ((target.os == Target::IOS) || target.os == Target::OSX)) {
            GTEST_SKIP() << "Skipping test for Vulkan on iOS/OSX (MoltenVK doesn't support dynamically allocated shared mem)";
        }
    }
};

TEST_F(GPUSpecializeTest, ProducerConsumerPairs) {
    Func f("f"), g("g"), h("h");
    Var x("x"), y("y");

    Param<bool> use_gpu;

    f(x, y) = x + y;
    g(x, y) = f(x - 1, y + 1) + f(x + 1, y - 1) + x;
    h(x, y) = g(x + 1, y - 1) + g(x - 1, y + 1) + y;

    // Specialize is a little tricky for producer-consumer pairs: the
    // compute_at must be the same in either case, which means you
    // must have a matching var name in either case.

    // Compute h in tiles either on the cpu or gpu
    Var xo("xo"), yo("yo"), xi("xi"), yi("yi"), t("t");
    h.compute_root().specialize(use_gpu).gpu_tile(x, y, xi, yi, 4, 4);
    h.tile(x, y, xo, yo, xi, yi, 8, 8).fuse(xo, yo, t).parallel(t);

    // Peel off a size-1 loop from blockidx to make a scheduling point
    // that matches the cpu case. We need to mark it as serial,
    // because by default when you split up a parallel loop both the
    // inside and outside are parallel.
    h.specialize(use_gpu).split(x, x, t, 1).serial(t);

    // Because t exists in both version of h, we can compute g at it.
    g.compute_at(h, t);

    // If we're on the gpu, we should map g's x and y to thread ids
    g.specialize(use_gpu).gpu_threads(x, y);

    // We want f compute_at g, x, so do the same trick to g;
    g.specialize(use_gpu).split(x, x, xi, 1).serial(xi);
    g.rename(x, xi);

    f.compute_at(g, xi);

    use_gpu.set(get_jit_target_from_environment().has_gpu_feature());
    Buffer<int> out1 = h.realize({1024, 1024});
    use_gpu.set(false);
    Buffer<int> out2 = h.realize({1024, 1024});

    for (int y = 0; y < out1.height(); y++) {
        for (int x = 0; x < out1.width(); x++) {
            int correct = 6 * x + 5 * y;
            EXPECT_EQ(out1(x, y), correct) << "out1 at (" << x << ", " << y << ")";
            EXPECT_EQ(out2(x, y), correct) << "out2 at (" << x << ", " << y << ")";
        }
    }
}

TEST_F(GPUSpecializeTest, SecondCase) {
    Func f("f"), g("g"), h("h");
    Var x("x"), y("y");

    Param<bool> p;

    f(x, y) = x + y;
    g(x, y) = f(x, y) + x;

    Var xo("xo"), yo("yo"), xi("xi"), yi("yi");
    f.specialize(p).tile(x, y, xi, yi, 4, 4).gpu_threads(x, y);
    f.tile(x, y, xo, yo, xi, yi, 8, 8).gpu_threads(xo, yo);

    f.compute_at(g, x);
    g.tile(x, y, xi, yi, 2, 2).gpu_blocks(x, y);

    p.set(true);
    Buffer<int> out = g.realize({32, 32});

    for (int y = 0; y < out.height(); y++) {
        for (int x = 0; x < out.width(); x++) {
            int correct = 2 * x + y;
            ASSERT_EQ(out(x, y), correct) << "at (" << x << ", " << y << ")";
        }
    }
}
