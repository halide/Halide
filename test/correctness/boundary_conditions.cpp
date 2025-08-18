#include "Halide.h"
#include "halide_test_error.h"
#include "test_sharding.h"

#include <algorithm>
#include <cstdio>

using namespace Halide;
using namespace Halide::BoundaryConditions;

namespace {

Var x("x"), y("y"), xo("xo"), yo("yo"), xi("xi"), yi("yi");
Target target = get_jit_target_from_environment();

void schedule_test(Func f, int vector_width, Partition partition_policy) {
    if (vector_width != 1) {
        f.vectorize(x, vector_width);
    }
    f.partition(x, partition_policy);
    f.partition(y, partition_policy);
    if (target.has_gpu_feature()) {
        f.gpu_tile(x, y, xo, yo, xi, yi, 2, 2);
    } else if (target.has_feature(Target::HVX)) {
        // TODO: Non-native vector widths hang the compiler here.
        // f.hexagon();
    }
}

template<typename T>
void check_constant_exterior(const Buffer<T> &input, T exterior, Func f,
                             int test_min_x, int test_extent_x, int test_min_y, int test_extent_y,
                             int vector_width, Partition partition_policy) {
    Buffer<T> result(test_extent_x, test_extent_y);
    result.set_min(test_min_x, test_min_y);
    f = lambda(x, y, f(x, y));
    schedule_test(f, vector_width, partition_policy);
    f.realize(result, target);
    result.copy_to_host();

    for (int32_t y = test_min_y; y < test_min_y + test_extent_y; y++) {
        for (int32_t x = test_min_x; x < test_min_x + test_extent_x; x++) {
            if (x < 0 || y < 0 || x >= input.width() || y >= input.height()) {
                EXPECT_EQ(result(x, y), exterior);
            } else {
                EXPECT_EQ(result(x, y), input(x, y));
            }
        }
    }
}

template<typename T>
void check_repeat_edge(const Buffer<T> &input, Func f,
                       int test_min_x, int test_extent_x, int test_min_y, int test_extent_y,
                       int vector_width, Partition partition_policy) {
    Buffer<T> result(test_extent_x, test_extent_y);
    result.set_min(test_min_x, test_min_y);
    f = lambda(x, y, f(x, y));
    schedule_test(f, vector_width, partition_policy);
    f.realize(result, target);
    result.copy_to_host();

    for (int32_t y = test_min_y; y < test_min_y + test_extent_y; y++) {
        for (int32_t x = test_min_x; x < test_min_x + test_extent_x; x++) {
            int32_t clamped_y = std::min(input.height() - 1, std::max(0, y));
            int32_t clamped_x = std::min(input.width() - 1, std::max(0, x));
            EXPECT_EQ(result(x, y), input(clamped_x, clamped_y));
        }
    }
}

template<typename T>
void check_repeat_image(const Buffer<T> &input, Func f,
                        int test_min_x, int test_extent_x, int test_min_y, int test_extent_y,
                        int vector_width, Partition partition_policy) {
    Buffer<T> result(test_extent_x, test_extent_y);
    result.set_min(test_min_x, test_min_y);
    f = lambda(x, y, f(x, y));
    schedule_test(f, vector_width, partition_policy);
    f.realize(result, target);
    result.copy_to_host();

    for (int32_t y = test_min_y; y < test_min_y + test_extent_y; y++) {
        for (int32_t x = test_min_x; x < test_min_x + test_extent_x; x++) {
            int32_t mapped_x = x;
            int32_t mapped_y = y;
            while (mapped_x < 0)
                mapped_x += input.width();
            while (mapped_x > input.width() - 1)
                mapped_x -= input.width();
            while (mapped_y < 0)
                mapped_y += input.height();
            while (mapped_y > input.height() - 1)
                mapped_y -= input.height();
            EXPECT_EQ(result(x, y), input(mapped_x, mapped_y));
        }
    }
}

template<typename T>
void check_mirror_image(const Buffer<T> &input, Func f,
                        int test_min_x, int test_extent_x, int test_min_y, int test_extent_y,
                        int vector_width, Partition partition_policy) {
    Buffer<T> result(test_extent_x, test_extent_y);
    result.set_min(test_min_x, test_min_y);
    f = lambda(x, y, f(x, y));
    schedule_test(f, vector_width, partition_policy);
    f.realize(result, target);
    result.copy_to_host();

    for (int32_t y = test_min_y; y < test_min_y + test_extent_y; y++) {
        for (int32_t x = test_min_x; x < test_min_x + test_extent_x; x++) {
            int32_t mapped_x = (x < 0) ? -(x + 1) : x;
            mapped_x = mapped_x % (2 * input.width());
            if (mapped_x > (input.width() - 1)) {
                mapped_x = (2 * input.width() - 1) - mapped_x;
            }
            int32_t mapped_y = (y < 0) ? -(y + 1) : y;
            mapped_y = mapped_y % (2 * input.height());
            if (mapped_y > (input.height() - 1)) {
                mapped_y = (2 * input.height() - 1) - mapped_y;
            }
            EXPECT_EQ(result(x, y), input(mapped_x, mapped_y));
        }
    }
}

template<typename T>
void check_mirror_interior(const Buffer<T> &input, Func f,
                           int test_min_x, int test_extent_x, int test_min_y, int test_extent_y,
                           int vector_width, Partition partition_policy) {
    Buffer<T> result(test_extent_x, test_extent_y);
    result.set_min(test_min_x, test_min_y);
    f = lambda(x, y, f(x, y));
    schedule_test(f, vector_width, partition_policy);
    f.realize(result, target);
    result.copy_to_host();

    for (int32_t y = test_min_y; y < test_min_y + test_extent_y; y++) {
        for (int32_t x = test_min_x; x < test_min_x + test_extent_x; x++) {
            int32_t mapped_x = abs(x) % (input.width() * 2 - 2);
            if (mapped_x > input.width() - 1) {
                mapped_x = input.width() * 2 - 2 - mapped_x;
            }
            int32_t mapped_y = abs(y) % (input.height() * 2 - 2);
            if (mapped_y > input.height() - 1) {
                mapped_y = input.height() * 2 - 2 - mapped_y;
            }

            EXPECT_EQ(result(x, y), input(mapped_x, mapped_y));
        }
    }
}

class BoundaryConditionsTest : public ::testing::TestWithParam<std::tuple<Partition, int>> {
public:
    static std::vector<int> vector_widths() {
        int vector_width_max = 32;
        if (target.has_feature(Target::OpenCL)) {
            vector_width_max = 16;
        }
        if (target.arch == Target::WebAssembly) {
            // The wasm jit is very slow, so shorten this test here.
            vector_width_max = 8;
        }
        if (target.has_feature(Target::Metal) ||
            target.has_feature(Target::Vulkan) ||
            target.has_feature(Target::D3D12Compute) ||
            target.has_feature(Target::WebGPU)) {
            // https://github.com/halide/Halide/issues/2148
            vector_width_max = 4;
        }
        std::vector<int> result;
        for (int i = 1; i <= vector_width_max; i *= 2) {
            result.push_back(i);
        }
        return result;
    }

protected:
    static constexpr int W = 32;
    static constexpr int H = 32;
    Buffer<uint8_t> input{W, H};

    Func input_f{"input_f"};

    static constexpr int32_t test_min = -25;
    static constexpr int32_t test_extent = 100;

    void SetUp() override {
        for (int32_t y = 0; y < H; y++) {
            for (int32_t x = 0; x < W; x++) {
                input(x, y) = x + y * W;
            }
        }
        input_f(x, y) = input(x, y);
    }
};

}  // namespace

TEST_P(BoundaryConditionsTest, RepeatEdgeFuncInput) {
    const auto &[partition_policy, vector_width] = GetParam();
    check_repeat_edge(
        input,
        repeat_edge(input_f, {{0, W}, {0, H}}),
        test_min, test_extent, test_min, test_extent,
        vector_width, partition_policy);
}

TEST_P(BoundaryConditionsTest, RepeatEdgeImageInput) {
    const auto &[partition_policy, vector_width] = GetParam();
    check_repeat_edge(
        input,
        repeat_edge(input, {{0, W}, {0, H}}),
        test_min, test_extent, test_min, test_extent,
        vector_width, partition_policy);
}

TEST_P(BoundaryConditionsTest, RepeatEdgeUndefinedBounds) {
    const auto &[partition_policy, vector_width] = GetParam();
    check_repeat_edge(
        input,
        repeat_edge(input, {{Expr(), Expr()}, {0, H}}),
        0, W, test_min, test_extent,
        vector_width, partition_policy);
    check_repeat_edge(
        input,
        repeat_edge(input, {{0, W}, {Expr(), Expr()}}),
        test_min, test_extent, 0, H,
        vector_width, partition_policy);
}

TEST_P(BoundaryConditionsTest, RepeatEdgeImplicitBounds) {
    const auto &[partition_policy, vector_width] = GetParam();
    check_repeat_edge(
        input,
        repeat_edge(input),
        test_min, test_extent, test_min, test_extent,
        vector_width, partition_policy);
}

TEST_P(BoundaryConditionsTest, ConstantExteriorFuncInput) {
    const auto &[partition_policy, vector_width] = GetParam();
    constexpr uint8_t exterior = 42;
    check_constant_exterior(
        input, exterior,
        constant_exterior(input_f, exterior, {{0, W}, {0, H}}),
        test_min, test_extent, test_min, test_extent,
        vector_width, partition_policy);
}

TEST_P(BoundaryConditionsTest, ConstantExteriorImageInput) {
    const auto &[partition_policy, vector_width] = GetParam();
    constexpr uint8_t exterior = 42;
    check_constant_exterior(
        input, exterior,
        constant_exterior(input, exterior, {{0, W}, {0, H}}),
        test_min, test_extent, test_min, test_extent,
        vector_width, partition_policy);
}

TEST_P(BoundaryConditionsTest, ConstantExteriorUndefinedBounds) {
    const auto &[partition_policy, vector_width] = GetParam();
    constexpr uint8_t exterior = 42;
    check_constant_exterior(
        input, exterior,
        constant_exterior(input, exterior, {{Expr(), Expr()}, {0, H}}),
        0, W, test_min, test_extent,
        vector_width, partition_policy);
    check_constant_exterior(
        input, exterior,
        constant_exterior(input, exterior, {{0, W}, {Expr(), Expr()}}),
        test_min, test_extent, 0, H,
        vector_width, partition_policy);
}

TEST_P(BoundaryConditionsTest, ConstantExteriorImplicitBounds) {
    const auto &[partition_policy, vector_width] = GetParam();
    constexpr uint8_t exterior = 42;
    check_constant_exterior(
        input, exterior,
        constant_exterior(input, exterior),
        test_min, test_extent, test_min, test_extent,
        vector_width, partition_policy);
}

TEST_P(BoundaryConditionsTest, RepeatImageFuncInput) {
    const auto &[partition_policy, vector_width] = GetParam();
    check_repeat_image(
        input,
        repeat_image(input_f, {{0, W}, {0, H}}),
        test_min, test_extent, test_min, test_extent,
        vector_width, partition_policy);
}

TEST_P(BoundaryConditionsTest, RepeatImageImageInput) {
    const auto &[partition_policy, vector_width] = GetParam();
    check_repeat_image(
        input,
        repeat_image(input, {{0, W}, {0, H}}),
        test_min, test_extent, test_min, test_extent,
        vector_width, partition_policy);
}

TEST_P(BoundaryConditionsTest, RepeatImageUndefinedBounds) {
    const auto &[partition_policy, vector_width] = GetParam();
    check_repeat_image(
        input,
        repeat_image(input, {{Expr(), Expr()}, {0, H}}),
        0, W, test_min, test_extent,
        vector_width, partition_policy);
    check_repeat_image(
        input,
        repeat_image(input, {{0, W}, {Expr(), Expr()}}),
        test_min, test_extent, 0, H,
        vector_width, partition_policy);
}

TEST_P(BoundaryConditionsTest, RepeatImageImplicitBounds) {
    const auto &[partition_policy, vector_width] = GetParam();
    check_repeat_image(
        input,
        repeat_image(input),
        test_min, test_extent, test_min, test_extent,
        vector_width, partition_policy);
}

TEST_P(BoundaryConditionsTest, MirrorImageFuncInput) {
    const auto &[partition_policy, vector_width] = GetParam();
    check_mirror_image(
        input,
        mirror_image(input_f, {{0, W}, {0, H}}),
        test_min, test_extent, test_min, test_extent,
        vector_width, partition_policy);
}

TEST_P(BoundaryConditionsTest, MirrorImageImageInput) {
    const auto &[partition_policy, vector_width] = GetParam();
    check_mirror_image(
        input,
        mirror_image(input, {{0, W}, {0, H}}),
        test_min, test_extent, test_min, test_extent,
        vector_width, partition_policy);
}

TEST_P(BoundaryConditionsTest, MirrorImageUndefinedBounds) {
    const auto &[partition_policy, vector_width] = GetParam();
    check_mirror_image(
        input,
        mirror_image(input, {{Expr(), Expr()}, {0, H}}),
        0, W, test_min, test_extent,
        vector_width, partition_policy);
    check_mirror_image(
        input,
        mirror_image(input, {{0, W}, {Expr(), Expr()}}),
        test_min, test_extent, 0, H,
        vector_width, partition_policy);
}

TEST_P(BoundaryConditionsTest, MirrorImageImplicitBounds) {
    const auto &[partition_policy, vector_width] = GetParam();
    check_mirror_image(
        input,
        mirror_image(input),
        test_min, test_extent, test_min, test_extent,
        vector_width, partition_policy);
}

TEST_P(BoundaryConditionsTest, MirrorInteriorFuncInput) {
    const auto &[partition_policy, vector_width] = GetParam();
    check_mirror_interior(
        input,
        mirror_interior(input_f, {{0, W}, {0, H}}),
        test_min, test_extent, test_min, test_extent,
        vector_width, partition_policy);
}

TEST_P(BoundaryConditionsTest, MirrorInteriorImageInput) {
    const auto &[partition_policy, vector_width] = GetParam();
    check_mirror_interior(
        input,
        mirror_interior(input, {{0, W}, {0, H}}),
        test_min, test_extent, test_min, test_extent,
        vector_width, partition_policy);
}

TEST_P(BoundaryConditionsTest, MirrorInteriorUndefinedBounds) {
    const auto &[partition_policy, vector_width] = GetParam();
    check_mirror_interior(
        input,
        mirror_interior(input, {{Expr(), Expr()}, {0, H}}),
        0, W, test_min, test_extent,
        vector_width, partition_policy);
    check_mirror_interior(
        input,
        mirror_interior(input, {{0, W}, {Expr(), Expr()}}),
        test_min, test_extent, 0, H,
        vector_width, partition_policy);
}

TEST_P(BoundaryConditionsTest, MirrorInteriorImplicitBounds) {
    const auto &[partition_policy, vector_width] = GetParam();
    check_mirror_interior(
        input,
        mirror_interior(input),
        test_min, test_extent, test_min, test_extent,
        vector_width, partition_policy);
}

INSTANTIATE_TEST_SUITE_P(
    BoundaryConditionsTest,
    BoundaryConditionsTest,
    testing::Combine(
        testing::Values(Partition::Auto, Partition::Never),
        testing::ValuesIn(BoundaryConditionsTest::vector_widths())));
