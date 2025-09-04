#include "Halide.h"
#include "HalideRuntimeOpenCL.h"
#include <gtest/gtest.h>

using namespace Halide;
using namespace Halide::Internal;

class GPUTextureTest : public ::testing::TestWithParam<MemoryType> {
protected:
    void SetUp() override {
        Target t = get_jit_target_from_environment();

        if (!t.has_feature(halide_target_feature_opencl)) {
            GTEST_SKIP() << "No OpenCL target enabled";
        }

        const auto *interface = get_device_interface_for_device_api(DeviceAPI::OpenCL);
        assert(interface->compute_capability != nullptr);
        int major, minor;
        int err = interface->compute_capability(nullptr, &major, &minor);
        if (err != 0 || (major == 1 && minor < 2)) {
            GTEST_SKIP() << "OpenCL " << major << "." << minor << " is less than required 1.2";
        }
    }
};

TEST_P(GPUTextureTest, OneDimensional) {
    auto memory_type = GetParam();
    // 1D stores/loads
    Buffer<int> input(100);
    input.fill(10);
    ImageParam param(Int(32), 1, "input");
    param.set(input);
    param.store_in(memory_type);  // check float stores

    Func f("f"), g("g");
    Var x("x"), xi("xi");
    Var y("y");

    f(x) = cast<float>(x);
    g(x) = param(x) + cast<int>(f(2 * x));

    g.gpu_tile(x, xi, 16);

    f.compute_root().store_in(memory_type).gpu_blocks(x);  // store f as integer
    g.output_buffer().store_in(memory_type);

    Buffer<int> out = g.realize({100});
    for (int x = 0; x < 100; x++) {
        int correct = 2 * x + 10;
        EXPECT_EQ(out(x), correct) << "1D memory_type=" << memory_type << " at x=" << x;
    }
}

TEST_P(GPUTextureTest, TwoDimensional) {
    auto memory_type = GetParam();
    // 2D stores/loads
    Buffer<int> input(10, 10);
    input.fill(10);
    ImageParam param(Int(32), 2);
    param.set(input);
    param.store_in(memory_type);  // check float stores

    Func f("f"), g("g");
    Var x("x"), xi("xi");
    Var y("y");

    f(x, y) = cast<float>(x + y);
    g(x) = param(x, x) + cast<int>(f(2 * x, x));

    g.gpu_tile(x, xi, 16, TailStrategy::GuardWithIf);

    f.compute_root().store_in(memory_type).gpu_blocks(x, y);  // store f as integer
    g.store_in(memory_type);

    Buffer<int> out = g.realize({10});
    for (int x = 0; x < 10; x++) {
        int correct = 3 * x + 10;
        EXPECT_EQ(out(x), correct) << "2D memory_type=" << memory_type << " at x=" << x;
    }
}

TEST_P(GPUTextureTest, ThreeDimensional) {
    auto memory_type = GetParam();
    // 3D stores/loads
    Buffer<int> input(10, 10, 10);
    input.fill(10);
    ImageParam param(Int(32), 3);
    param.set(input);
    param.store_in(memory_type);  // check float stores

    Func f("f"), g("g");
    Var x("x"), xi("xi");
    Var y("y"), z("z");

    f(x, y, z) = cast<float>(x + y + z);
    g(x) = param(x, x, x) + cast<int>(f(2 * x, x, x));

    g.gpu_tile(x, xi, 16, TailStrategy::GuardWithIf);

    f.compute_root().store_in(memory_type).gpu_blocks(x, y, z);  // store f as integer

    g.store_in(memory_type);

    Buffer<int> out = g.realize({10});
    for (int x = 0; x < 10; x++) {
        int correct = 4 * x + 10;
        EXPECT_EQ(out(x), correct) << "3D memory_type=" << memory_type << " at x=" << x;
    }
}

TEST_P(GPUTextureTest, OneDimensionalOffset) {
    auto memory_type = GetParam();
    // 1D offset
    Buffer<int> input(100);
    input.set_min(5);
    input.fill(10);
    ImageParam param(Int(32), 1);
    param.set(input);
    param.store_in(memory_type);  // check float stores

    Func f("f"), g("g");
    Var x("x"), xi("xi");
    Var y("y");

    f(x) = cast<float>(x);
    g(x) = param(x) + cast<int>(f(2 * x));

    g.gpu_tile(x, xi, 16, TailStrategy::GuardWithIf);

    f.compute_root().store_in(memory_type).gpu_blocks(x);  // store f as integer
    g.store_in(memory_type);

    Buffer<int> out(10);
    out.set_min(10);
    g.realize(out);
    out.copy_to_host();
    for (int x = 10; x < 20; x++) {
        int correct = 2 * x + 10;
        EXPECT_EQ(out(x), correct) << "1D-shift memory_type=" << memory_type << " at x=" << x;
    }
}

INSTANTIATE_TEST_SUITE_P(
    MemoryTypes,
    GPUTextureTest,
    ::testing::Values(MemoryType::GPUTexture, MemoryType::Heap));
