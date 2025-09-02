#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

TEST(GPUGiveInputBuffersDeviceAllocations, Basic) {
#ifdef WITH_SERIALIZATION_JIT_ROUNDTRIP_TESTING
    GTEST_SKIP() << "Serialization won't preserve GPU buffers, skipping.";
#endif

    Target t(get_jit_target_from_environment());
    if (!t.has_gpu_feature()) {
        GTEST_SKIP() << "No GPU target enabled.";
    }

    // Make an uninitialized host buffer
    Buffer<float> in(100, 100);

    // Check it's considered uninitialized
    EXPECT_FALSE(in.has_device_allocation());
    EXPECT_FALSE(in.host_dirty());
    EXPECT_FALSE(in.device_dirty());

    // Fill it with a value, and check it's considered initialized on
    // the host.
    in.fill(7.0f);
    EXPECT_FALSE(in.has_device_allocation());
    EXPECT_TRUE(in.host_dirty());
    EXPECT_FALSE(in.device_dirty());

    // Run a pipeline that uses it as an input
    Func f;
    Var x, y, xi, yi;
    f(x, y) = in(x, y);
    f.gpu_tile(x, y, xi, yi, 8, 8);
    Buffer<float> out = f.realize({100, 100});

    // Check the output has a device allocation, and was copied to
    // host by realize.
    EXPECT_TRUE(out.has_device_allocation());
    EXPECT_FALSE(out.host_dirty());
    EXPECT_FALSE(out.device_dirty());

    // Check the input now has a device allocation, and was
    // successfully copied to device.
    EXPECT_TRUE(in.has_device_allocation());
    EXPECT_FALSE(in.host_dirty());
    EXPECT_FALSE(in.device_dirty());

    // Run the pipeline again into the same output. This variant of
    // realize doesn't do a copy-back.
    f.realize(out);

    // out still has a device allocation, but now it's dirty.
    EXPECT_TRUE(out.has_device_allocation());
    EXPECT_FALSE(out.host_dirty());
    EXPECT_TRUE(out.device_dirty());

    // in has not changed
    EXPECT_TRUE(in.has_device_allocation());
    EXPECT_FALSE(in.host_dirty());
    EXPECT_FALSE(in.device_dirty());
}
