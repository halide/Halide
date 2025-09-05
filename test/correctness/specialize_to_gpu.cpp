#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

TEST(SpecializeToGpuTest, SpecializeToGpu) {
#ifdef WITH_SERIALIZATION_JIT_ROUNDTRIP_TESTING
    GTEST_SKIP() << "Serialization won't preserve GPU buffers, skipping.";
#endif

    if (!get_jit_target_from_environment().has_gpu_feature()) {
        GTEST_SKIP() << "No GPU target enabled.";
    }

    // A sequence of stages which may or may not run on the gpu.
    Func f, g, h;
    ImageParam in(Int(32), 1);
    Var x, xi;

    f(x) = in(x) + in(x + 1);
    g(x) = f(x * 2);
    h(x) = g(x) - 7;

    Param<bool> gpu_f, gpu_g, gpu_h;

    f.compute_root().specialize(gpu_f).gpu_tile(x, x, xi, 16);
    g.compute_root().specialize(gpu_g).gpu_tile(x, x, xi, 16);
    h.compute_root().specialize(gpu_h).gpu_tile(x, x, xi, 16);

    Buffer<int> out(128), reference(128), input(256);

    ASSERT_NO_THROW(lambda(x, x * 17 + 43 + x * x).realize(input));
    in.set(input);

    gpu_f.set(false);
    gpu_g.set(false);
    gpu_h.set(false);
    ASSERT_NO_THROW(h.realize(reference));

    for (int i = 1; i < 8; i++) {
        gpu_f.set((i & 1) != 0);
        gpu_g.set((i & 2) != 0);
        gpu_h.set((i & 4) != 0);

        ASSERT_NO_THROW(h.realize(out));

        RDom r(out);
        uint32_t err = evaluate<uint32_t>(sum(abs(out(r) - reference(r))));
        EXPECT_EQ(err, 0) << "Incorrect results for test " << i;
    }
}
