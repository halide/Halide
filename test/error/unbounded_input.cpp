#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestUnboundedInput() {
    Func f;
    Var x, y;

    ImageParam in(Float(32), 2);
    ImageParam x_coord(Int(32), 2);
    ImageParam y_coord(Int(32), 2);

    f(x, y) = in(x_coord(x, y), y_coord(x, y));

    f.compile_jit();
}
}  // namespace

TEST(ErrorTests, UnboundedInput) {
    EXPECT_COMPILE_ERROR(TestUnboundedInput, MatchesPattern(R"(Buffer p\d+ may be accessed in an unbounded way in dimension 0)"));
}
