#include "Halide.h"
#include "HalideBuffer.h"
#include <gtest/gtest.h>

using namespace Halide;

namespace {
const int kEdges[3] = {128, 64, 32};

Runtime::Buffer<int32_t> make_gpu_buffer(bool hexagon_rpc) {
    Var x, y, c;
    Func f;
    f(x, y, c) = x + y * 256 + c * 256 * 256;

    if (hexagon_rpc) {
        f.hexagon();
    } else {
        Var xi, yi;
        f.gpu_tile(x, y, xi, yi, 8, 8);
    }

    Buffer<int32_t> result = f.realize({kEdges[0], kEdges[1], kEdges[2]});
    return *result.get();
}

class DeviceSliceTest : public testing::Test {
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

TEST_F(DeviceSliceTest, InPlaceSlicing) {
    Runtime::Buffer<int32_t> gpu_buf = make_gpu_buffer(hexagon_rpc);
    ASSERT_NE(gpu_buf.raw_buffer()->device_interface, nullptr);

    const int slice_dim = 1;
    const int slice_pos = 0;
    gpu_buf.slice(slice_dim, slice_pos);
    ASSERT_NE(gpu_buf.raw_buffer()->device_interface, nullptr);

    ASSERT_EQ(gpu_buf.dimensions(), 2);
    EXPECT_EQ(gpu_buf.extent(0), kEdges[0]);
    EXPECT_EQ(gpu_buf.extent(1), kEdges[2]);

    gpu_buf.copy_to_host();
    gpu_buf.for_each_element([&](int x, int c) {
        const int y = slice_pos;
        EXPECT_EQ(gpu_buf(x, c), x + y * 256 + c * 256 * 256);
    });
}

TEST_F(DeviceSliceTest, NondestructiveSlicing) {
    Runtime::Buffer<int32_t> gpu_buf = make_gpu_buffer(hexagon_rpc);
    ASSERT_NE(gpu_buf.raw_buffer()->device_interface, nullptr);

    const int slice_dim = 0;
    const int slice_pos = 31;
    Runtime::Buffer<int32_t> sliced = gpu_buf.sliced(slice_dim, slice_pos);
    ASSERT_NE(sliced.raw_buffer()->device_interface, nullptr);

    ASSERT_EQ(sliced.dimensions(), 2);
    EXPECT_EQ(sliced.extent(0), kEdges[1]);
    EXPECT_EQ(sliced.extent(1), kEdges[2]);

    sliced.copy_to_host();
    sliced.for_each_element([&](int y, int c) {
        const int x = slice_pos;
        EXPECT_EQ(sliced(y, c), x + y * 256 + c * 256 * 256);
    });

    gpu_buf.copy_to_host();
    gpu_buf.for_each_element([&](int x, int y, int c) {
        EXPECT_EQ(gpu_buf(x, y, c), x + y * 256 + c * 256 * 256);
    });
}

TEST_F(DeviceSliceTest, NondestructiveSlicingWithStaticDims) {
    Runtime::Buffer<int32_t, 3> gpu_buf = make_gpu_buffer(hexagon_rpc);
    ASSERT_NE(gpu_buf.raw_buffer()->device_interface, nullptr);

    const int slice_dim = 0;
    const int slice_pos = 31;
    Runtime::Buffer<int32_t, 2> sliced = gpu_buf.sliced(slice_dim, slice_pos);
    ASSERT_NE(sliced.raw_buffer()->device_interface, nullptr);

    ASSERT_EQ(sliced.dimensions(), 2);
    EXPECT_EQ(sliced.extent(0), kEdges[1]);
    EXPECT_EQ(sliced.extent(1), kEdges[2]);

    sliced.copy_to_host();
    sliced.for_each_element([&](int y, int c) {
        const int x = slice_pos;
        EXPECT_EQ(sliced(y, c), x + y * 256 + c * 256 * 256);
    });

    gpu_buf.copy_to_host();
    gpu_buf.for_each_element([&](int x, int y, int c) {
        EXPECT_EQ(gpu_buf(x, y, c), x + y * 256 + c * 256 * 256);
    });
}

TEST_F(DeviceSliceTest, SliceOfSlice) {
    Runtime::Buffer<int32_t> gpu_buf = make_gpu_buffer(hexagon_rpc);
    ASSERT_NE(gpu_buf.raw_buffer()->device_interface, nullptr);

    const int slice_dim = 1;
    const int slice_pos = 0;
    Runtime::Buffer<int32_t> sliced = gpu_buf.sliced(slice_dim, slice_pos);
    ASSERT_NE(sliced.raw_buffer()->device_interface, nullptr);

    ASSERT_EQ(sliced.dimensions(), 2);
    EXPECT_EQ(sliced.extent(0), kEdges[0]);
    EXPECT_EQ(sliced.extent(1), kEdges[2]);

    const int slice_dim2 = 0;
    const int slice_pos2 = 10;
    Runtime::Buffer<int32_t> sliced2 = sliced.sliced(slice_dim2, slice_pos2);
    ASSERT_NE(sliced2.raw_buffer()->device_interface, nullptr);

    ASSERT_EQ(sliced2.dimensions(), 1);
    EXPECT_EQ(sliced2.extent(0), kEdges[2]);

    sliced.copy_to_host();
    sliced.for_each_element([&](int x, int c) {
        const int y = slice_pos;
        EXPECT_EQ(sliced(x, c), x + y * 256 + c * 256 * 256);
    });

    sliced2.copy_to_host();
    sliced2.for_each_element([&](int c) {
        const int x = slice_pos2;
        const int y = slice_pos;
        EXPECT_EQ(sliced2(c), x + y * 256 + c * 256 * 256);
    });

    gpu_buf.copy_to_host();
    gpu_buf.for_each_element([&](int x, int y, int c) {
        EXPECT_EQ(gpu_buf(x, y, c), x + y * 256 + c * 256 * 256);
    });
}

TEST_F(DeviceSliceTest, ParentOutOfScopeBeforeSlice) {
    Runtime::Buffer<int32_t> sliced;

    const int slice_dim = 1;
    const int slice_pos = 0;

    {
        Runtime::Buffer<int32_t> gpu_buf = make_gpu_buffer(hexagon_rpc);
        ASSERT_NE(gpu_buf.raw_buffer()->device_interface, nullptr);

        sliced = gpu_buf.sliced(slice_dim, slice_pos);
        ASSERT_NE(sliced.raw_buffer()->device_interface, nullptr);
    }

    ASSERT_EQ(sliced.dimensions(), 2);
    EXPECT_EQ(sliced.extent(0), kEdges[0]);
    EXPECT_EQ(sliced.extent(1), kEdges[2]);

    sliced.copy_to_host();
    sliced.for_each_element([&](int x, int c) {
        const int y = slice_pos;
        EXPECT_EQ(sliced(x, c), x + y * 256 + c * 256 * 256);
    });
}

TEST_F(DeviceSliceTest, RealizeToFromSlice) {
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

    const int slice_dim = 1;
    const int slice_pos = 0;

    gpu_input.slice(slice_dim, slice_pos);
    gpu_output.slice(slice_dim, slice_pos);

    in.set(gpu_input);

    EXPECT_NO_THROW(f.realize(gpu_output));

    gpu_output.copy_to_host();
    gpu_output.for_each_element([&](int x, int c) {
        const int y = slice_pos;
        EXPECT_EQ(gpu_output(x, c), x + y * 256 + c * 256 * 256 + 42);
    });
}
