#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestDefineAfterUse() {
    Func f, g;
    Var x;

    f(x) = x;
    g(x) = f(x) + 1;

    // Now try to add an update definition to f
    f(x) += 1;
}
}  // namespace

TEST(ErrorTests, DefineAfterUse) {
    EXPECT_COMPILE_ERROR(
        TestDefineAfterUse,
        MatchesPattern(R"(Func f\d+ cannot be given a new update definition, )"
                       R"(because it has already been realized or used in the )"
                       R"(definition of another Func\.)"));
}
