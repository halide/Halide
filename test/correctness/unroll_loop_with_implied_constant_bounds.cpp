#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

namespace {
enum class ScheduleType {
    Unroll,
    Vectorize
};
}  // namespace

class UnrollLoopWithImpliedConstantBoundsTest : public ::testing::TestWithParam<ScheduleType> {};
TEST_P(UnrollLoopWithImpliedConstantBoundsTest, ImpliedConstantBounds) {
    ScheduleType schedule_type = GetParam();

    // This test verifies that unrolling/vectorizing is capable of inferring
    // constant bounds of loops that are implied by containing if statement
    // conditions, e.g the following structure should work:

    /*
      let extent = foo
      if (foo == 7) {
        unrolled for (x from 0 to extent) {...}
      }
    */

    Func intermediate("intermediate");
    Func output1("output1"), output2("output2");

    Var x("x"), y("y"), c("c");

    intermediate(x, y, c) = x + y + c;

    output1(x, y, c) = intermediate(x, y, c);
    output2(x, y, c) = intermediate(x, y, c);

    Expr three_channels =
        (output1.output_buffer().dim(2).extent() == 3 &&
         output1.output_buffer().dim(2).min() == 0 &&
         output2.output_buffer().dim(2).extent() == 3 &&
         output2.output_buffer().dim(2).min() == 0);

    switch (schedule_type) {
    case ScheduleType::Unroll:
        intermediate.compute_root()
            .specialize(three_channels)
            .unroll(c);
        break;
    case ScheduleType::Vectorize:
        intermediate.compute_root()
            .specialize(three_channels)
            .vectorize(c);
        break;
    }

    Pipeline p{{output1, output2}};

    // Should not throw an error in loop unrolling or vectorization.
    ASSERT_NO_THROW(p.compile_jit());
}

INSTANTIATE_TEST_SUITE_P(
    ScheduleTypes,
    UnrollLoopWithImpliedConstantBoundsTest,
    ::testing::Values(ScheduleType::Unroll, ScheduleType::Vectorize));
