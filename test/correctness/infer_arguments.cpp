#include "Halide.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace Halide;
using namespace Halide::Internal;

namespace {
#define HL_T(x) (type_of<std::decay_t<decltype(x)>>())
MATCHER_P(EqConstantExpr, expected,
          "a constant Expr of type " + ::testing::PrintToString(HL_T(expected)) + " equal to " + ::testing::PrintToString(expected)) {
    if (arg.type() != HL_T(expected)) {
        *result_listener << "has type " << arg.type() << " instead of " << HL_T(expected);
        return false;
    }
    if (!can_prove(arg == Expr(expected))) {
        *result_listener << "does not equal constant " << expected;
        return false;
    }
    return true;
}
}  // namespace

TEST(InferArgumentsTest, Basic) {
    ImageParam input1(UInt(8), 3, "input1");
    ImageParam input2(UInt(8), 2, "input2");
    Param<int32_t> height("height");
    Param<int32_t> width("width");
    Param<uint8_t> thresh("thresh");
    Param<float> frac("frac", 22.5f, 11.25f, 1e30f);
    // Named so that it will come last.
    const uint64_t kU64 = 0xf00dcafedeadbeef;
    Param<uint64_t> z_unsigned("z_unsigned", 0xdeadbeef, 0x01, Expr(kU64));

    Var x("x"), y("y"), c("c");

    Func f("f");
    f(x, y, c) = frac * (input1(clamp(x, 0, height), clamp(y, 0, width), c) +
                         min(thresh, input2(x, y))) +
                 (0 * z_unsigned);

    std::vector<Argument> args = f.infer_arguments();
    EXPECT_EQ(args.size(), 7);

    Argument input1_arg = args[0];
    Argument input2_arg = args[1];
    Argument frac_arg = args[2];
    Argument height_arg = args[3];
    Argument thresh_arg = args[4];
    Argument width_arg = args[5];
    Argument z_unsigned_arg = args[6];

    EXPECT_EQ(input1_arg.name, "input1");
    EXPECT_EQ(input2_arg.name, "input2");
    EXPECT_EQ(frac_arg.name, "frac");
    EXPECT_EQ(height_arg.name, "height");
    EXPECT_EQ(thresh_arg.name, "thresh");
    EXPECT_EQ(width_arg.name, "width");
    EXPECT_EQ(z_unsigned_arg.name, "z_unsigned");

    EXPECT_TRUE(input1_arg.is_buffer());
    EXPECT_TRUE(input2_arg.is_buffer());
    EXPECT_FALSE(frac_arg.is_buffer());
    EXPECT_FALSE(height_arg.is_buffer());
    EXPECT_FALSE(thresh_arg.is_buffer());
    EXPECT_FALSE(width_arg.is_buffer());
    EXPECT_FALSE(z_unsigned_arg.is_buffer());

    // All Scalar Arguments have a defined default when coming from
    // infer_arguments.
    EXPECT_FALSE(input1_arg.argument_estimates.scalar_def.defined());
    EXPECT_FALSE(input2_arg.argument_estimates.scalar_def.defined());
    EXPECT_TRUE(frac_arg.argument_estimates.scalar_def.defined());
    EXPECT_THAT(frac_arg.argument_estimates.scalar_def, EqConstantExpr(22.5f));
    EXPECT_TRUE(height_arg.argument_estimates.scalar_def.defined());
    EXPECT_TRUE(thresh_arg.argument_estimates.scalar_def.defined());
    EXPECT_TRUE(width_arg.argument_estimates.scalar_def.defined());
    EXPECT_TRUE(z_unsigned_arg.argument_estimates.scalar_def.defined());
    EXPECT_THAT(z_unsigned_arg.argument_estimates.scalar_def, EqConstantExpr<uint64_t>(0xdeadbeef));

    EXPECT_FALSE(input1_arg.argument_estimates.scalar_min.defined());
    EXPECT_FALSE(input2_arg.argument_estimates.scalar_min.defined());
    EXPECT_TRUE(frac_arg.argument_estimates.scalar_min.defined());
    EXPECT_THAT(frac_arg.argument_estimates.scalar_min, EqConstantExpr(11.25f));
    EXPECT_FALSE(height_arg.argument_estimates.scalar_min.defined());
    EXPECT_FALSE(thresh_arg.argument_estimates.scalar_min.defined());
    EXPECT_FALSE(width_arg.argument_estimates.scalar_min.defined());
    EXPECT_TRUE(z_unsigned_arg.argument_estimates.scalar_min.defined());
    EXPECT_THAT(z_unsigned_arg.argument_estimates.scalar_min, EqConstantExpr<uint64_t>(0x1));

    EXPECT_FALSE(input1_arg.argument_estimates.scalar_max.defined());
    EXPECT_FALSE(input2_arg.argument_estimates.scalar_max.defined());
    EXPECT_TRUE(frac_arg.argument_estimates.scalar_max.defined());
    EXPECT_THAT(frac_arg.argument_estimates.scalar_max, EqConstantExpr(1e30f));
    EXPECT_FALSE(height_arg.argument_estimates.scalar_max.defined());
    EXPECT_FALSE(thresh_arg.argument_estimates.scalar_max.defined());
    EXPECT_FALSE(width_arg.argument_estimates.scalar_max.defined());
    EXPECT_TRUE(z_unsigned_arg.argument_estimates.scalar_max.defined());
    EXPECT_THAT(z_unsigned_arg.argument_estimates.scalar_max, EqConstantExpr<uint64_t>(0xf00dcafedeadbeef));

    EXPECT_EQ(input1_arg.dimensions, 3);
    EXPECT_EQ(input2_arg.dimensions, 2);
    EXPECT_EQ(frac_arg.dimensions, 0);
    EXPECT_EQ(height_arg.dimensions, 0);
    EXPECT_EQ(thresh_arg.dimensions, 0);
    EXPECT_EQ(width_arg.dimensions, 0);
    EXPECT_EQ(z_unsigned_arg.dimensions, 0);

    EXPECT_TRUE(frac_arg.type.code() == Type::Float);
    EXPECT_TRUE(height_arg.type.code() == Type::Int);
    EXPECT_TRUE(thresh_arg.type.code() == Type::UInt);
    EXPECT_TRUE(width_arg.type.code() == Type::Int);
    EXPECT_TRUE(z_unsigned_arg.type.code() == Type::UInt);

    EXPECT_EQ(frac_arg.type.bits(), 32);
    EXPECT_EQ(height_arg.type.bits(), 32);
    EXPECT_EQ(thresh_arg.type.bits(), 8);
    EXPECT_EQ(width_arg.type.bits(), 32);
    EXPECT_EQ(z_unsigned_arg.type.bits(), 64);

    Func f_a("f_a"), f_b("f_b");
    f_a(x, y, c) = input1(x, y, c) * frac;
    f_b(x, y, c) = input1(x, y, c) + thresh;
    Func f_tuple("f_tuple");
    f_tuple(x, y, c) = Tuple(f_a(x, y, c), f_b(x, y, c));

    args = f_tuple.infer_arguments();
    EXPECT_EQ(args.size(), 3);

    input1_arg = args[0];
    frac_arg = args[1];
    thresh_arg = args[2];

    EXPECT_EQ(input1_arg.name, "input1");
    EXPECT_EQ(frac_arg.name, "frac");
    EXPECT_EQ(thresh_arg.name, "thresh");

    EXPECT_TRUE(input1_arg.is_buffer());
    EXPECT_FALSE(frac_arg.is_buffer());
    EXPECT_FALSE(thresh_arg.is_buffer());

    EXPECT_EQ(input1_arg.dimensions, 3);
    EXPECT_EQ(frac_arg.dimensions, 0);
    EXPECT_EQ(thresh_arg.dimensions, 0);

    EXPECT_EQ(frac_arg.type.code(), Type::Float);
    EXPECT_EQ(thresh_arg.type.code(), Type::UInt);

    EXPECT_EQ(frac_arg.type.bits(), 32);
    EXPECT_EQ(thresh_arg.type.bits(), 8);
}
