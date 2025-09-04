#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

namespace {
Expr u8(Expr a) {
    return cast<uint8_t>(a);
}
}  // namespace

class LogicalTest : public ::testing::Test {
protected:
    Buffer<uint8_t> input{128, 64};
    Var x, y, xi, yi;

    void SetUp() override {
        for (int y = 0; y < input.height(); y++) {
            for (int x = 0; x < input.width(); x++) {
                input(x, y) = y * input.width() + x;
            }
        }
    }
};

TEST_F(LogicalTest, BasicLogicalOperations) {
    Func f;
    f(x, y) = select(((input(x, y) > 10) && (input(x, y) < 20)) ||
                         ((input(x, y) > 40) && (!(input(x, y) > 50))),
                     u8(255), u8(0));

    Target target = get_jit_target_from_environment();
    if (target.has_gpu_feature()) {
        f.gpu_tile(x, y, xi, yi, 16, 16);
        f.vectorize(xi, 4);
    } else if (target.has_feature(Target::HVX)) {
        f.hexagon().vectorize(x, 128);
    } else {
        f.vectorize(x, 8);
    }

    Buffer<uint8_t> output = f.realize({input.width(), input.height()}, target);

    for (int y = 0; y < input.height(); y++) {
        for (int x = 0; x < input.width(); x++) {
            bool cond = ((input(x, y) > 10) && (input(x, y) < 20)) ||
                        ((input(x, y) > 40) && (!(input(x, y) > 50)));
            uint8_t correct = cond ? 255 : 0;
            EXPECT_EQ(output(x, y), correct) << "output(" << x << ", " << y << ") = " << (int)output(x, y) << " instead of " << (int)correct;
        }
    }
}

TEST_F(LogicalTest, CommonSubexpressionElimination) {
    // Test a condition that uses a let resulting from common
    // subexpression elimination.
    Func f;
    Expr common_cond = input(x, y) > 10;
    f(x, y) = select((common_cond && (input(x, y) < 20)) ||
                         ((input(x, y) > 40) && (!common_cond)),
                     u8(255), u8(0));

    Target target = get_jit_target_from_environment();
    if (target.has_gpu_feature()) {
        f.gpu_tile(x, y, xi, yi, 16, 16);
        f.vectorize(xi, 4);
    } else if (target.has_feature(Target::HVX)) {
        f.hexagon().vectorize(x, 128);
    } else {
        f.vectorize(x, 8);
    }

    Buffer<uint8_t> output = f.realize({input.width(), input.height()}, target);

    for (int y = 0; y < input.height(); y++) {
        for (int x = 0; x < input.width(); x++) {
            bool common_cond = input(x, y) > 10;
            bool cond = (common_cond && (input(x, y) < 20)) ||
                        ((input(x, y) > 40) && (!common_cond));
            uint8_t correct = cond ? 255 : 0;
            EXPECT_EQ(output(x, y), correct) << "output(" << x << ", " << y << ") = " << (int)output(x, y) << " instead of " << (int)correct;
        }
    }
}

TEST_F(LogicalTest, VectorAndScalarInputs) {
    // Test a condition which has vector and scalar inputs.
    Func f("f");
    f(x, y) = select(x < 10 || x > 20 || y < 10 || y > 20, 0, input(x, y));

    Target target = get_jit_target_from_environment();

    if (target.has_gpu_feature()) {
        f.gpu_tile(x, y, xi, yi, 16, 16);
        f.vectorize(xi, 4);
    } else if (target.has_feature(Target::HVX)) {
        f.hexagon().vectorize(x, 128);
    } else {
        f.vectorize(x, 128);
    }

    Buffer<uint8_t> output = f.realize({input.width(), input.height()}, target);

    for (int y = 0; y < input.height(); y++) {
        for (int x = 0; x < input.width(); x++) {
            bool cond = x < 10 || x > 20 || y < 10 || y > 20;
            uint8_t correct = cond ? 0 : input(x, y);
            EXPECT_EQ(output(x, y), correct) << "output(" << x << ", " << y << ") = " << (int)output(x, y) << " instead of " << (int)correct;
        }
    }
}

TEST_F(LogicalTest, DifferentlySizedTypes) {
    // Test a condition that uses differently sized types.
    Func f;
    Expr ten = 10;
    f(x, y) = select(input(x, y) > ten, u8(255), u8(0));

    Target target = get_jit_target_from_environment();
    if (target.has_gpu_feature()) {
        f.gpu_tile(x, y, xi, yi, 16, 16);
        f.vectorize(xi, 4);
    } else if (target.has_feature(Target::HVX)) {
        f.hexagon().vectorize(x, 128);
    } else {
        f.vectorize(x, 8);
    }

    Buffer<uint8_t> output = f.realize({input.width(), input.height()}, target);

    for (int y = 0; y < input.height(); y++) {
        for (int x = 0; x < input.width(); x++) {
            bool cond = input(x, y) > 10;
            uint8_t correct = cond ? 255 : 0;
            EXPECT_EQ(output(x, y), correct) << "output(" << x << ", " << y << ") = " << (int)output(x, y) << " instead of " << (int)correct;
        }
    }
}

class SelectWithDifferentConditionWidthTest : public LogicalTest,
                                              public ::testing::WithParamInterface<std::pair<int, int>> {
};

TEST_P(SelectWithDifferentConditionWidthTest, DifferentWidths) {
    auto [narrow_bits, wide_bits] = GetParam();

    Target target = get_jit_target_from_environment();
    if (target.has_feature(Target::OpenCL) && narrow_bits == 16 && wide_bits == 32) {
        // Workaround for https://github.com/halide/Halide/issues/2477
        GTEST_SKIP() << "Skipping uint" << narrow_bits << " -> uint" << wide_bits << " for OpenCL";
    }

    Type narrow = UInt(narrow_bits), wide = UInt(wide_bits);

    Func in_wide;
    in_wide(x, y) = cast(wide, y + x * 3);
    in_wide.compute_root();

    Func in_narrow;
    in_narrow(x, y) = cast(narrow, x * y + x - 17);
    in_narrow.compute_root();

    Func f;
    f(x, y) = select(in_narrow(x, y) > 10, in_wide(x, y * 2), in_wide(x, y * 2 + 1));

    Func cpu;
    cpu(x, y) = f(x, y);

    Func gpu;
    gpu(x, y) = f(x, y);

    Func out;
    out(x, y) = {cast<uint32_t>(cpu(x, y)), cast<uint32_t>(gpu(x, y))};

    cpu.compute_root();
    gpu.compute_root();

    if (target.has_gpu_feature()) {
        gpu.gpu_tile(x, y, xi, yi, 16, 16);
        gpu.vectorize(xi, 4);
    } else if (target.has_feature(Target::HVX)) {
        gpu.hexagon().vectorize(x, 128);
    } else {
        // Just test vectorization
        gpu.vectorize(x, 8);
    }

    Realization r = out.realize({input.width(), input.height()}, target);
    Buffer<uint32_t> cpu_output = r[0];
    Buffer<uint32_t> gpu_output = r[1];

    for (int y = 0; y < input.height(); y++) {
        for (int x = 0; x < input.width(); x++) {
            EXPECT_EQ(gpu_output(x, y), cpu_output(x, y))
                << "gpu_output(" << x << ", " << y << ") = " << gpu_output(x, y)
                << " instead of " << cpu_output(x, y)
                << " for uint" << narrow_bits << " -> uint" << wide_bits;
        }
    }
}

INSTANTIATE_TEST_SUITE_P(WidthCombinations, SelectWithDifferentConditionWidthTest,
                         ::testing::Values(
                             std::make_pair(8, 16),
                             std::make_pair(8, 32),
                             std::make_pair(16, 32)));
