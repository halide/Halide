#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestUnboundedOutput() {
    Func f;
    Var x, y;

    ImageParam in(Float(32), 2);
    ImageParam x_coord(Int(32), 2);
    ImageParam y_coord(Int(32), 2);

    f(x, y) = 0.0f;
    RDom r(0, 100, 0, 100);
    f(x_coord(r.x, r.y), y_coord(r.x, r.y)) += in(r.x, r.y);

    f.compile_jit();
}
}  // namespace

TEST(ErrorTests, UnboundedOutput) {
    EXPECT_COMPILE_ERROR(
        TestUnboundedOutput,
        MatchesPattern(R"(Update definition number 0 of Function f\d+ calls function )"
                       R"(f\d+ in an unbounded way in dimension 0)"));
}
