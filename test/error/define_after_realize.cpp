#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestDefineAfterRealize() {
    Func f, g;
    Var x;

    f(x) = x;

    Buffer<int> im = f.realize({10});

    // Now try to add an update definition to f
    f(x) += 1;
}
}  // namespace

TEST(ErrorTests, DefineAfterRealize) {
    EXPECT_COMPILE_ERROR(
        TestDefineAfterRealize,
        MatchesPattern(R"(Func f\d+ cannot be given a new update definition, )"
                       R"(because it has already been realized or used in the )"
                       R"(definition of another Func\.)"));
}
