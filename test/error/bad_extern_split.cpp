#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestBadExternSplit() {
    Func f;
    Var x;
    f.define_extern("test", {}, Int(32), {x});
    Var xo;
    f.split(x, xo, x, 8).reorder(xo, x);

    f.compile_jit();
}
}  // namespace

TEST(ErrorTests, BadExternSplit) {
    EXPECT_COMPILE_ERROR(TestBadExternSplit, MatchesPattern(R"(Externally defined Func f\d+ cannot have extern loop v\d+\.v\d+ outside a non-extern loop\.)"));
}
