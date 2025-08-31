#include "Halide.h"
#include "HalideBuffer.h"
#include "HalideRuntime.h"
#include <gtest/gtest.h>

using namespace Halide;

namespace {
Runtime::Buffer<int32_t> make_gpu_buffer(DeviceAPI api, int offset = 0) {
    Var x, y;
    Func f;
    f(x, y) = x + y * 256 + offset;

    if (api == DeviceAPI::Hexagon) {
        f.hexagon();
    } else {
        Var xi, yi;
        f.gpu_tile(x, y, xi, yi, 8, 8, TailStrategy::Auto, api);
    }

    Buffer<int32_t> result = f.realize({128, 128});
    return *result.get();
}

class DeviceBufferCopyTest : public ::testing::Test {
protected:
    Target target{get_jit_target_from_environment()};
    DeviceAPI device_api{target.get_required_device_api()};

    void SetUp() override {
        if (device_api == DeviceAPI::None) {
            GTEST_SKIP() << "No GPU target enabled.";
        }
    }
};

class CrossDeviceBufferCopyTest : public DeviceBufferCopyTest {
protected:
    DeviceAPI second_device_api{
        target.without_feature(target_feature_for_device_api(device_api))
            .get_required_device_api()};

    void SetUp() override {
        DeviceBufferCopyTest::SetUp();
        if (second_device_api == DeviceAPI::None) {
            GTEST_SKIP() << "No second GPU target enabled.";
        }
    }
};
}  // namespace

TEST_F(DeviceBufferCopyTest, CopyToDevice) {
    Runtime::Buffer<int32_t> gpu_buf = make_gpu_buffer(device_api);
    ASSERT_NE(gpu_buf.raw_buffer()->device_interface, nullptr);

    Runtime::Buffer<int32_t> cpu_buf(128, 128);
    cpu_buf.fill(0);
    EXPECT_EQ(gpu_buf.raw_buffer()->device_interface->buffer_copy(
                  nullptr, cpu_buf, gpu_buf.raw_buffer()->device_interface, gpu_buf),
              0);

    gpu_buf.copy_to_host();
    for (int i = 0; i < 128; i++) {
        for (int j = 0; j < 128; j++) {
            EXPECT_EQ(gpu_buf(i, j), 0);
        }
    }
}

TEST_F(DeviceBufferCopyTest, CopyFromDevice) {
    Runtime::Buffer<int32_t> gpu_buf = make_gpu_buffer(device_api);
    ASSERT_NE(gpu_buf.raw_buffer()->device_interface, nullptr);

    Runtime::Buffer<int32_t> cpu_buf(128, 128);
    EXPECT_EQ(gpu_buf.raw_buffer()->device_interface->buffer_copy(
                  nullptr, gpu_buf, nullptr, cpu_buf),
              0);

    for (int i = 0; i < 128; i++) {
        for (int j = 0; j < 128; j++) {
            EXPECT_EQ(cpu_buf(i, j), i + j * 256);
        }
    }
}

TEST_F(DeviceBufferCopyTest, CopyDeviceToDevice) {
    Runtime::Buffer<int32_t> gpu_buf1 = make_gpu_buffer(device_api);
    ASSERT_NE(gpu_buf1.raw_buffer()->device_interface, nullptr);
    Runtime::Buffer<int32_t> gpu_buf2 = make_gpu_buffer(device_api, 256000);
    ASSERT_NE(gpu_buf2.raw_buffer()->device_interface, nullptr);

    EXPECT_EQ(gpu_buf1.raw_buffer()->device_interface->buffer_copy(
                  nullptr, gpu_buf2, gpu_buf1.raw_buffer()->device_interface, gpu_buf1),
              0);
    gpu_buf1.copy_to_host();

    for (int i = 0; i < 128; i++) {
        for (int j = 0; j < 128; j++) {
            EXPECT_EQ(gpu_buf1(i, j), i + j * 256 + 256000);
        }
    }
}

TEST_F(DeviceBufferCopyTest, CopyHostToDeviceSubset) {
    Runtime::Buffer<int32_t> cpu_buf(128, 128);
    cpu_buf.fill(0);

    Runtime::Buffer<int32_t> gpu_buf1 = make_gpu_buffer(device_api);
    ASSERT_NE(gpu_buf1.raw_buffer()->device_interface, nullptr);

    Runtime::Buffer<int32_t> gpu_buf2 = gpu_buf1.cropped({{32, 64}, {32, 64}});
    ASSERT_NE(gpu_buf2.raw_buffer()->device_interface, nullptr);

    EXPECT_EQ(gpu_buf1.raw_buffer()->device_interface->buffer_copy(
                  nullptr, cpu_buf, gpu_buf2.raw_buffer()->device_interface, gpu_buf2),
              0);
    gpu_buf1.set_device_dirty();
    gpu_buf1.copy_to_host();

    for (int i = 0; i < 128; i++) {
        for (int j = 0; j < 128; j++) {
            bool in_gpu2 = i >= gpu_buf2.dim(0).min() && i <= gpu_buf2.dim(0).max() &&
                           j >= gpu_buf2.dim(1).min() && j <= gpu_buf2.dim(1).max();
            EXPECT_EQ(gpu_buf1(i, j), in_gpu2 ? 0 : i + j * 256);
        }
    }
}

TEST_F(DeviceBufferCopyTest, CopyDeviceToHostSubset) {
    Runtime::Buffer<int32_t> cpu_buf(128, 128);
    cpu_buf.fill(0);
    Runtime::Buffer<int32_t> cpu_buf1 = cpu_buf.cropped({{32, 64}, {32, 64}});

    Runtime::Buffer<int32_t> gpu_buf = make_gpu_buffer(device_api);
    ASSERT_NE(gpu_buf.raw_buffer()->device_interface, nullptr);

    EXPECT_EQ(gpu_buf.raw_buffer()->device_interface->buffer_copy(
                  nullptr, gpu_buf, nullptr, cpu_buf1),
              0);

    for (int i = 0; i < 128; i++) {
        for (int j = 0; j < 128; j++) {
            bool in_cpu1 = i >= cpu_buf1.dim(0).min() && i <= cpu_buf1.dim(0).max() &&
                           j >= cpu_buf1.dim(1).min() && j <= cpu_buf1.dim(1).max();
            EXPECT_EQ(cpu_buf(i, j), in_cpu1 ? i + j * 256 : 0);
        }
    }
}

TEST_F(DeviceBufferCopyTest, CopyDeviceToDeviceSubsetBigToSmall) {
    Runtime::Buffer<int32_t> gpu_buf1 = make_gpu_buffer(device_api);
    ASSERT_NE(gpu_buf1.raw_buffer()->device_interface, nullptr);

    Runtime::Buffer<int32_t> gpu_buf2 = make_gpu_buffer(device_api, 256000);
    ASSERT_NE(gpu_buf2.raw_buffer()->device_interface, nullptr);

    Runtime::Buffer<int32_t> gpu_buf3 = gpu_buf2.cropped({{32, 64}, {32, 64}});
    ASSERT_NE(gpu_buf3.raw_buffer()->device_interface, nullptr);

    EXPECT_EQ(gpu_buf1.raw_buffer()->device_interface->buffer_copy(
                  nullptr, gpu_buf1, gpu_buf3.raw_buffer()->device_interface, gpu_buf3),
              0);
    gpu_buf2.set_device_dirty();
    gpu_buf2.copy_to_host();

    for (int i = 0; i < 128; i++) {
        for (int j = 0; j < 128; j++) {
            bool in_gpu3 = i >= gpu_buf3.dim(0).min() && i <= gpu_buf3.dim(0).max() &&
                           j >= gpu_buf3.dim(1).min() && j <= gpu_buf3.dim(1).max();
            EXPECT_EQ(gpu_buf2(i, j), i + j * 256 + (in_gpu3 ? 0 : 256000));
        }
    }
}

TEST_F(DeviceBufferCopyTest, CopyDeviceToDeviceSubsetSmallToBig) {
    Runtime::Buffer<int32_t> gpu_buf1 = make_gpu_buffer(device_api);
    ASSERT_NE(gpu_buf1.raw_buffer()->device_interface, nullptr);

    Runtime::Buffer<int32_t> gpu_buf2 = make_gpu_buffer(device_api, 256000);
    ASSERT_NE(gpu_buf2.raw_buffer()->device_interface, nullptr);

    Runtime::Buffer<int32_t> gpu_buf3 = gpu_buf2.cropped({{32, 64}, {32, 64}});
    ASSERT_NE(gpu_buf3.raw_buffer()->device_interface, nullptr);

    EXPECT_EQ(gpu_buf3.raw_buffer()->device_interface->buffer_copy(
                  nullptr, gpu_buf3, gpu_buf1.raw_buffer()->device_interface, gpu_buf1),
              0);
    gpu_buf1.set_device_dirty();
    gpu_buf1.copy_to_host();

    for (int i = 0; i < 128; i++) {
        for (int j = 0; j < 128; j++) {
            bool in_gpu3 = i >= gpu_buf3.dim(0).min() && i <= gpu_buf3.dim(0).max() &&
                           j >= gpu_buf3.dim(1).min() && j <= gpu_buf3.dim(1).max();
            EXPECT_EQ(gpu_buf1(i, j), i + j * 256 + (in_gpu3 ? 256000 : 0));
        }
    }
}

TEST_F(DeviceBufferCopyTest, CopyDeviceToDeviceSubsetDisjointCrops) {
    Runtime::Buffer<int32_t> gpu_buf1 = make_gpu_buffer(device_api);
    ASSERT_NE(gpu_buf1.raw_buffer()->device_interface, nullptr);

    Runtime::Buffer<int32_t> gpu_buf2 = make_gpu_buffer(device_api, 256000);
    ASSERT_NE(gpu_buf2.raw_buffer()->device_interface, nullptr);

    Runtime::Buffer<int32_t> gpu_buf1_crop = gpu_buf1.cropped({{32, 64}, {32, 16}});
    Runtime::Buffer<int32_t> gpu_buf2_crop = gpu_buf2.cropped({{32, 16}, {32, 64}});
    ASSERT_NE(gpu_buf1_crop.raw_buffer()->device_interface, nullptr);
    ASSERT_NE(gpu_buf2_crop.raw_buffer()->device_interface, nullptr);

    EXPECT_EQ(gpu_buf2_crop.raw_buffer()->device_interface->buffer_copy(
                  nullptr, gpu_buf2_crop, gpu_buf1_crop.raw_buffer()->device_interface, gpu_buf1_crop),
              0);
    gpu_buf1.set_device_dirty();
    gpu_buf1.copy_to_host();

    for (int i = 0; i < 128; i++) {
        for (int j = 0; j < 128; j++) {
            bool in_gpu1_crop = i >= gpu_buf1_crop.dim(0).min() && i <= gpu_buf1_crop.dim(0).max() &&
                                j >= gpu_buf1_crop.dim(1).min() && j <= gpu_buf1_crop.dim(1).max();
            bool in_gpu2_crop = i >= gpu_buf2_crop.dim(0).min() && i <= gpu_buf2_crop.dim(0).max() &&
                                j >= gpu_buf2_crop.dim(1).min() && j <= gpu_buf2_crop.dim(1).max();
            EXPECT_EQ(gpu_buf1(i, j), i + j * 256 + (in_gpu1_crop && in_gpu2_crop ? 256000 : 0));
        }
    }
}

TEST_F(DeviceBufferCopyTest, CopyFromDeviceNoSourceHost) {
    Runtime::Buffer<int32_t> gpu_buf = make_gpu_buffer(device_api);
    ASSERT_NE(gpu_buf.raw_buffer()->device_interface, nullptr);
    halide_buffer_t no_host_src = *gpu_buf.raw_buffer();
    no_host_src.host = nullptr;
    no_host_src.set_device_dirty(false);

    Runtime::Buffer<int32_t> cpu_buf(128, 128);
    EXPECT_EQ(gpu_buf.raw_buffer()->device_interface->buffer_copy(
                  nullptr, &no_host_src, nullptr, cpu_buf),
              0);

    for (int i = 0; i < 128; i++) {
        for (int j = 0; j < 128; j++) {
            EXPECT_EQ(cpu_buf(i, j), i + j * 256);
        }
    }
}

TEST_F(DeviceBufferCopyTest, CopyToDeviceNoDestHost) {
    Runtime::Buffer<int32_t> gpu_buf = make_gpu_buffer(device_api);
    ASSERT_NE(gpu_buf.raw_buffer()->device_interface, nullptr);
    halide_buffer_t no_host_dst = *gpu_buf.raw_buffer();
    no_host_dst.host = nullptr;

    Runtime::Buffer<int32_t> cpu_buf(128, 128);
    cpu_buf.fill(0);
    EXPECT_EQ(gpu_buf.raw_buffer()->device_interface->buffer_copy(
                  nullptr, cpu_buf, gpu_buf.raw_buffer()->device_interface, &no_host_dst),
              0);
    gpu_buf.set_device_dirty(true);

    gpu_buf.copy_to_host();
    for (int i = 0; i < 128; i++) {
        for (int j = 0; j < 128; j++) {
            EXPECT_EQ(gpu_buf(i, j), 0);
        }
    }
}

TEST_F(DeviceBufferCopyTest, CopyDeviceToHostNoDestHostErrors) {
    Runtime::Buffer<int32_t> gpu_buf1 = make_gpu_buffer(device_api);
    ASSERT_NE(gpu_buf1.raw_buffer()->device_interface, nullptr);
    halide_buffer_t no_host_dst = *gpu_buf1.raw_buffer();
    no_host_dst.host = nullptr;

    Runtime::Buffer<int32_t> gpu_buf2 = make_gpu_buffer(device_api, 256000);
    ASSERT_NE(gpu_buf2.raw_buffer()->device_interface, nullptr);

    EXPECT_EQ(gpu_buf1.raw_buffer()->device_interface->buffer_copy(
                  nullptr, gpu_buf2, nullptr, &no_host_dst),
              halide_error_code_host_is_null);
}

TEST_F(CrossDeviceBufferCopyTest, CrossDeviceCopyDeviceToDevice) {
    Runtime::Buffer<int32_t> gpu_buf1 = make_gpu_buffer(device_api, 0);
    ASSERT_NE(gpu_buf1.raw_buffer()->device_interface, nullptr);

    Runtime::Buffer<int32_t> gpu_buf2 = make_gpu_buffer(second_device_api, 256000);
    ASSERT_NE(gpu_buf2.raw_buffer()->device_interface, nullptr);

    EXPECT_EQ(gpu_buf1.raw_buffer()->device_interface->buffer_copy(
                  nullptr, gpu_buf2, gpu_buf1.raw_buffer()->device_interface, gpu_buf1),
              0);
    gpu_buf1.copy_to_host();

    for (int i = 0; i < 128; i++) {
        for (int j = 0; j < 128; j++) {
            EXPECT_EQ(gpu_buf1(i, j), i + j * 256 + 256000);
        }
    }
}

TEST_F(CrossDeviceBufferCopyTest, CrossDeviceCopyDeviceToDeviceSubset) {
    Runtime::Buffer<int32_t> gpu_buf1 = make_gpu_buffer(device_api, 0);
    ASSERT_NE(gpu_buf1.raw_buffer()->device_interface, nullptr);

    Runtime::Buffer<int32_t> gpu_buf2 = make_gpu_buffer(second_device_api, 256000);
    ASSERT_NE(gpu_buf2.raw_buffer()->device_interface, nullptr);

    Runtime::Buffer<int32_t> gpu_buf3 = gpu_buf2.cropped({{32, 64}, {32, 64}});
    ASSERT_NE(gpu_buf3.raw_buffer()->device_interface, nullptr);

    EXPECT_EQ(gpu_buf1.raw_buffer()->device_interface->buffer_copy(
                  nullptr, gpu_buf1, gpu_buf3.raw_buffer()->device_interface, gpu_buf3),
              0);
    gpu_buf2.set_device_dirty();
    gpu_buf2.copy_to_host();

    for (int i = 0; i < 128; i++) {
        for (int j = 0; j < 128; j++) {
            bool in_gpu3 = i >= gpu_buf3.dim(0).min() && i < gpu_buf3.dim(0).min() + gpu_buf3.dim(0).extent() &&
                           j >= gpu_buf3.dim(1).min() && j < gpu_buf3.dim(1).min() + gpu_buf3.dim(1).extent();
            EXPECT_EQ(gpu_buf2(i, j), i + j * 256 + (in_gpu3 ? 0 : 256000));
        }
    }
}

TEST_F(CrossDeviceBufferCopyTest, CrossDeviceCopyDeviceToDeviceNoSourceHost) {
    Runtime::Buffer<int32_t> gpu_buf1 = make_gpu_buffer(device_api, 0);
    ASSERT_NE(gpu_buf1.raw_buffer()->device_interface, nullptr);

    Runtime::Buffer<int32_t> gpu_buf2 = make_gpu_buffer(second_device_api, 256000);
    ASSERT_NE(gpu_buf2.raw_buffer()->device_interface, nullptr);
    halide_buffer_t no_host_src = *gpu_buf2.raw_buffer();
    no_host_src.host = nullptr;

    EXPECT_EQ(gpu_buf1.raw_buffer()->device_interface->buffer_copy(
                  nullptr, &no_host_src, gpu_buf1.raw_buffer()->device_interface, gpu_buf1),
              0);
    gpu_buf1.copy_to_host();

    for (int i = 0; i < 128; i++) {
        for (int j = 0; j < 128; j++) {
            EXPECT_EQ(gpu_buf1(i, j), i + j * 256 + 256000);
        }
    }
}

TEST_F(CrossDeviceBufferCopyTest, CrossDeviceCopyDeviceToDeviceNoDestHost) {
    Runtime::Buffer<int32_t> gpu_buf1 = make_gpu_buffer(device_api, 0);
    ASSERT_NE(gpu_buf1.raw_buffer()->device_interface, nullptr);
    halide_buffer_t no_host_dst = *gpu_buf1.raw_buffer();
    no_host_dst.host = nullptr;

    Runtime::Buffer<int32_t> gpu_buf2 = make_gpu_buffer(second_device_api, 256000);
    ASSERT_NE(gpu_buf2.raw_buffer()->device_interface, nullptr);

    EXPECT_EQ(gpu_buf1.raw_buffer()->device_interface->buffer_copy(
                  nullptr, gpu_buf2, gpu_buf1.raw_buffer()->device_interface, &no_host_dst),
              0);
    gpu_buf1.set_device_dirty();
    gpu_buf1.copy_to_host();

    for (int i = 0; i < 128; i++) {
        for (int j = 0; j < 128; j++) {
            EXPECT_EQ(gpu_buf1(i, j), i + j * 256 + 256000);
        }
    }
}

TEST_F(CrossDeviceBufferCopyTest, CrossDeviceCopyDeviceToDeviceNoSourceOrDestHost) {
    Runtime::Buffer<int32_t> gpu_buf1 = make_gpu_buffer(device_api, 0);
    ASSERT_NE(gpu_buf1.raw_buffer()->device_interface, nullptr);
    halide_buffer_t no_host_dst = *gpu_buf1.raw_buffer();
    no_host_dst.host = nullptr;

    Runtime::Buffer<int32_t> gpu_buf2 = make_gpu_buffer(second_device_api, 256000);
    ASSERT_NE(gpu_buf2.raw_buffer()->device_interface, nullptr);
    halide_buffer_t no_host_src = *gpu_buf2.raw_buffer();
    no_host_src.host = nullptr;

    int err = gpu_buf1.raw_buffer()->device_interface->buffer_copy(
        nullptr, &no_host_src, gpu_buf1.raw_buffer()->device_interface, &no_host_dst);
    if (err == 0) {
        gpu_buf1.set_device_dirty();
        gpu_buf1.copy_to_host();
        for (int i = 0; i < 128; i++) {
            for (int j = 0; j < 128; j++) {
                EXPECT_EQ(gpu_buf1(i, j), i + j * 256 + 256000);
            }
        }
    } else {
        EXPECT_EQ(err, halide_error_code_incompatible_device_interface);
    }
}

TEST_F(CrossDeviceBufferCopyTest, CrossDeviceCopyDeviceToHost) {
    Runtime::Buffer<int32_t> gpu_buf1 = make_gpu_buffer(device_api, 0);
    ASSERT_NE(gpu_buf1.raw_buffer()->device_interface, nullptr);

    Runtime::Buffer<int32_t> gpu_buf2 = make_gpu_buffer(second_device_api, 256000);
    ASSERT_NE(gpu_buf2.raw_buffer()->device_interface, nullptr);

    EXPECT_EQ(gpu_buf1.raw_buffer()->device_interface->buffer_copy(
                  nullptr, gpu_buf1, nullptr, gpu_buf2),
              0);
    gpu_buf1.set_device_dirty();
    gpu_buf1.copy_to_host();

    for (int i = 0; i < 128; i++) {
        for (int j = 0; j < 128; j++) {
            EXPECT_EQ(gpu_buf1(i, j), i + j * 256);
        }
    }
}

TEST_F(CrossDeviceBufferCopyTest, CrossDeviceCopyDeviceToHostNoDestHost) {
    Runtime::Buffer<int32_t> gpu_buf1 = make_gpu_buffer(device_api, 0);
    ASSERT_NE(gpu_buf1.raw_buffer()->device_interface, nullptr);
    halide_buffer_t no_host_dst = *gpu_buf1.raw_buffer();
    no_host_dst.host = nullptr;

    Runtime::Buffer<int32_t> gpu_buf2 = make_gpu_buffer(second_device_api, 256000);
    ASSERT_NE(gpu_buf2.raw_buffer()->device_interface, nullptr);

    EXPECT_EQ(gpu_buf1.raw_buffer()->device_interface->buffer_copy(
                  nullptr, gpu_buf1, nullptr, &no_host_dst),
              halide_error_code_host_is_null);
}
