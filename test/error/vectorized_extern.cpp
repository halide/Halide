#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestVectorizedExtern() {
    Func f;
    Var x;
    f.define_extern("test", {}, Int(32), {x});
    Var xo;
    f.split(x, xo, x, 8).vectorize(xo);

    f.compile_jit();
}
}  // namespace

TEST(ErrorTests, VectorizedExtern) {
    EXPECT_COMPILE_ERROR(TestVectorizedExtern, MatchesPattern(R"(Externally defined Func f\d+ cannot have loop type vectorized \(v\d+\.v\d+\))"));
}
