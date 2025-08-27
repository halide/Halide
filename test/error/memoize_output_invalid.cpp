#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestMemoizeOutputInvalid() {
    Var x{"x"};
    Func f{"f"};
    f(x) = 0.0f;
    f(x) += 1;
    f.memoize();

    f.realize({3});
}
}  // namespace

TEST(ErrorTests, MemoizeOutputInvalid) {
    EXPECT_COMPILE_ERROR(
        TestMemoizeOutputInvalid,
        MatchesPattern(R"(Can't compile Pipeline with memoized output Func: )"
                       R"(f(\$\d+)?. Memoization is valid only on intermediate )"
                       R"(Funcs because it takes control of buffer allocation.)"));
}
