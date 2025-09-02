#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

TEST(GPUJITExplicitCopyToDevice, Basic) {
    Target target = get_jit_target_from_environment();

    if (!target.has_gpu_feature()) {
        GTEST_SKIP() << "No GPU target enabled.";
    }

    // We'll have two input buffers. For one we'll copy to the device
    // explicitly. For the other we'll do a device malloc and set
    // host_dirty.
    for (DeviceAPI d : {DeviceAPI::Default_GPU, DeviceAPI::CUDA, DeviceAPI::OpenCL}) {
        if (!get_device_interface_for_device_api(d)) {
            continue;
        }

        Buffer<float> a(100, 100), b(100, 100);

        EXPECT_FALSE(a.host_dirty());
        a.fill(2.0f);
        EXPECT_FALSE(a.has_device_allocation());
        EXPECT_TRUE(a.host_dirty());
        a.copy_to_device(d);
        EXPECT_TRUE(a.has_device_allocation());
        EXPECT_FALSE(a.host_dirty());

        EXPECT_FALSE(b.host_dirty());
        b.fill(3.0f);
        EXPECT_FALSE(b.has_device_allocation());
        EXPECT_TRUE(b.host_dirty());
        b.device_malloc(d);
        EXPECT_TRUE(b.has_device_allocation());
        EXPECT_TRUE(b.host_dirty());

        Func f;
        Var x, y, tx, ty;
        f(x, y) = a(x, y) + b(x, y) + 2;
        f.gpu_tile(x, y, tx, ty, 8, 8, TailStrategy::Auto, d);

        Buffer<float> out = f.realize({100, 100});

        for (int yy = 0; yy < out.height(); yy++) {
            for (int xx = 0; xx < out.width(); xx++) {
                ASSERT_EQ(out(xx, yy), 7.0f) << "at (" << xx << "," << yy << ") for DeviceAPI=" << (int)d;
            }
        }
    }
}
