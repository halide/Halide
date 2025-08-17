#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestReusedArgs() {
    Func f;
    Var x;
    // You can't use the same variable more than once in the LHS of a
    // pure definition.
    f(x, x) = x;
}
}  // namespace

TEST(ErrorTests, ReusedArgs) {
    EXPECT_COMPILE_ERROR(TestReusedArgs, MatchesPattern(R"(In pure definition of Func \"f\d+\":\nIn left-hand-side of definition, arguments 1 and 0 both have the name \"v\d+\")"));
}
