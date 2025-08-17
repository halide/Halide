#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestBadSchedule() {
    Func f, g;
    Var x, y;

    f(x) = x;
    g(x) = f(x);

    // f is inlined, so this schedule is bad.
    f.vectorize(x, 4);

    g.realize({10});
}
}  // namespace

TEST(ErrorTests, BadSchedule) {
    EXPECT_COMPILE_ERROR(TestBadSchedule, HasSubstr("TODO"));
}
