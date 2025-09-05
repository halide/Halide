#include "Halide.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace Halide;

TEST(PrintLoopNestTest, Basic) {
    Func output_y{"output_y"}, output_u{"output_u"}, output_v{"output_v"};
    Var x{"x"}, y{"y"}, xo{"xo"}, yo{"yo"}, xi{"xi"}, yi{"yi"};
    Buffer<int> input(960, 960, 3);
    output_y(x, y) = input(x, y, 0);
    output_u(x, y) = input(2 * x, 2 * y, 1);
    output_v(x, y) = input(2 * x, 2 * y, 2);
    output_u.tile(x, y, xo, yo, xi, yi, 4, 1);
    output_v.tile(x, y, xo, yo, xi, yi, 4, 1);
    output_y.tile(x, y, xo, yo, xi, yi, 4 * 2, 2);

    output_u.compute_with(output_y, xo);
    output_v.compute_with(output_y, xo);

    Pipeline p({output_y, output_u, output_v});
    testing::internal::CaptureStderr();
    EXPECT_NO_THROW(p.print_loop_nest());
    EXPECT_THAT(
        testing::internal::GetCapturedStderr(),
        testing::AllOf(
            testing::HasSubstr("produce output_y:"),
            testing::HasSubstr("  produce output_v:"),
            testing::HasSubstr("    produce output_u:"),
            testing::HasSubstr("      for y.fused.yo:"),
            testing::HasSubstr("        for x.fused.xo:"),
            testing::HasSubstr("          for y.yi in [0, 1]:"),
            testing::HasSubstr("            for x.xi in [0, 7]:"),
            testing::HasSubstr("              output_y(...) = ..."),
            testing::HasSubstr("          for x.xi in [0, 3]:"),
            testing::HasSubstr("            output_u(...) = ..."),
            testing::HasSubstr("          for x.xi in [0, 3]:"),
            testing::HasSubstr("            output_v(...) = ...")));
}
