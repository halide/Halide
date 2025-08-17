#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestComputeWithFuseInSpecialization() {
    Var x("x"), y("y"), f("f");
    ImageParam in(Int(16), 2, "in");
    Func out0("out0"), out1("out1");
    out0(x, y) = 1 * in(x, y);
    out1(x, y) = 2 * in(x, y);

    out0.vectorize(x, 8, TailStrategy::RoundUp);
    out1.vectorize(x, 8, TailStrategy::RoundUp).compute_with(out0, x);

    out0.specialize(in.dim(1).stride() == 128).fuse(x, y, f);
    Pipeline p({out0, out1});
    p.compile_jit();
}
}  // namespace

TEST(ErrorTests, ComputeWithFuseInSpecialization) {
    EXPECT_COMPILE_ERROR(
        TestComputeWithFuseInSpecialization,
        MatchesPattern(R"(Invalid compute_with: cannot find x in )"
                       R"(out\d+(\$\d+)?\.s\d+)"));
}
