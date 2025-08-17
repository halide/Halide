#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestUndefinedLoopLevel() {
    LoopLevel undefined;

    Var x;
    Func f, g;
    f(x) = x;
    g(x) = f(x);
    f.compute_at(undefined);
    g.compute_root();

    // Trying to lower/realize with an undefined LoopLevel should be fatal
    Buffer<int> result = g.realize({1});
}
}  // namespace

TEST(ErrorTests, UndefinedLoopLevel) {
    EXPECT_COMPILE_ERROR(
        TestUndefinedLoopLevel,
        HasSubstr("There should be no undefined LoopLevels at the start of "
                  "lowering. (Did you mean to use LoopLevel::inlined() "
                  "instead of LoopLevel() ?)"));
}
