#include "Halide.h"
#include "HalideBuffer.h"
#include <gtest/gtest.h>

using namespace Halide;

namespace {
Runtime::Buffer<int32_t> make_gpu_buffer(bool hexagon_rpc) {
    Var x, y;
    Func f;
    f(x, y) = x + y * 256;

    if (hexagon_rpc) {
        f.hexagon();
    } else {
        Var xi, yi;
        f.gpu_tile(x, y, xi, yi, 8, 8);
    }

    Buffer<int32_t> result = f.realize({128, 128});
    return *result.get();
}

class DeviceCropTest : public testing::Test {
protected:
    void SetUp() override {
        target = get_jit_target_from_environment();

        hexagon_rpc = (target.arch != Target::Hexagon) &&
                      target.has_feature(Target::HVX);

        if (!hexagon_rpc && !target.has_gpu_feature()) {
            GTEST_SKIP() << "No GPU target enabled.";
        }
    }

    Target target;
    bool hexagon_rpc = false;
};
}  // namespace

TEST_F(DeviceCropTest, InPlaceCropping) {
    Runtime::Buffer<int32_t> gpu_buf = make_gpu_buffer(hexagon_rpc);
    ASSERT_NE(gpu_buf.raw_buffer()->device_interface, nullptr);

    gpu_buf.crop({{32, 64}, {32, 64}});
    ASSERT_NE(gpu_buf.raw_buffer()->device_interface, nullptr);

    gpu_buf.copy_to_host();
    for (int i = 0; i < 64; i++) {
        for (int j = 0; j < 64; j++) {
            EXPECT_EQ(gpu_buf(32 + i, 32 + j), (i + 32) + 256 * (j + 32));
        }
    }
}

TEST_F(DeviceCropTest, NondestructiveCropping) {
    Runtime::Buffer<int32_t> gpu_buf = make_gpu_buffer(hexagon_rpc);
    ASSERT_NE(gpu_buf.raw_buffer()->device_interface, nullptr);

    Runtime::Buffer<int32_t> cropped = gpu_buf.cropped({{32, 64}, {32, 64}});
    ASSERT_NE(cropped.raw_buffer()->device_interface, nullptr);

    cropped.copy_to_host();
    for (int i = 0; i < 64; i++) {
        for (int j = 0; j < 64; j++) {
            EXPECT_EQ(cropped(32 + i, 32 + j), (i + 32) + 256 * (j + 32));
        }
    }
}

TEST_F(DeviceCropTest, CropOfCrop) {
    Runtime::Buffer<int32_t> gpu_buf = make_gpu_buffer(hexagon_rpc);
    ASSERT_NE(gpu_buf.raw_buffer()->device_interface, nullptr);

    Runtime::Buffer<int32_t> cropped = gpu_buf.cropped({{32, 64}, {32, 64}});
    ASSERT_NE(cropped.raw_buffer()->device_interface, nullptr);

    Runtime::Buffer<int32_t> cropped2 = cropped.cropped({{40, 16}, {40, 16}});
    ASSERT_NE(cropped2.raw_buffer()->device_interface, nullptr);

    cropped.copy_to_host();
    for (int i = 0; i < 64; i++) {
        for (int j = 0; j < 64; j++) {
            EXPECT_EQ(cropped(32 + i, 32 + j), (i + 32) + 256 * (j + 32));
        }
    }

    cropped2.copy_to_host();
    for (int i = 0; i < 16; i++) {
        for (int j = 0; j < 16; j++) {
            EXPECT_EQ(cropped2(40 + i, 40 + j), (i + 40) + 256 * (j + 40));
        }
    }
}

TEST_F(DeviceCropTest, ParentOutOfScopeBeforeCrop) {
    Runtime::Buffer<int32_t> cropped;

    {
        Runtime::Buffer<int32_t> gpu_buf = make_gpu_buffer(hexagon_rpc);
        ASSERT_NE(gpu_buf.raw_buffer()->device_interface, nullptr);

        cropped = gpu_buf.cropped({{32, 64}, {32, 64}});
        ASSERT_NE(cropped.raw_buffer()->device_interface, nullptr);
    }

    cropped.copy_to_host();
    for (int i = 0; i < 64; i++) {
        for (int j = 0; j < 64; j++) {
            EXPECT_EQ(cropped(32 + i, 32 + j), (i + 32) + 256 * (j + 32));
        }
    }
}

TEST_F(DeviceCropTest, RealizeToFromCrop) {
    // TODO: This test currently fails on macOS with target host-opencl.
    if (target.os == Target::OSX && target.has_feature(Target::OpenCL)) {
        GTEST_SKIP() << "This test currently fails on macOS with target host-opencl.";
    }

    ImageParam in(Int(32), 2);
    Var x, y;
    Func f;
    f(x, y) = in(x, y) + 42;

    Var xi, yi;
    if (hexagon_rpc) {
        f.hexagon();
    } else {
        f.gpu_tile(x, y, xi, yi, 8, 8);
    }

    Buffer<int32_t> gpu_input = make_gpu_buffer(hexagon_rpc);
    Buffer<int32_t> gpu_output = make_gpu_buffer(hexagon_rpc);

    gpu_input.crop({{64, 64}, {64, 64}});
    gpu_output.crop({{64, 64}, {64, 64}});

    in.set(gpu_input);

    EXPECT_NO_THROW(f.realize(gpu_output));

    gpu_output.copy_to_host();
    for (int i = 0; i < 64; i++) {
        for (int j = 0; j < 64; j++) {
            EXPECT_EQ(gpu_output(64 + i, 64 + j), (i + 64) + 256 * (j + 64) + 42);
        }
    }
}
