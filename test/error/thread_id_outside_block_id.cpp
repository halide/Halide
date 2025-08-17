#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestThreadIdOutsideBlockId() {
    Target t = get_jit_target_from_environment();
    t.set_feature(Target::CUDA);

    Func f;
    Var x;
    f(x) = x;
    Var xo, xi;
    f.gpu_tile(x, xo, xi, 16).reorder(xo, xi);

    f.compile_jit(t);
    Buffer<int> result = f.realize({16});
}
}  // namespace

TEST(ErrorTests, ThreadIdOutsideBlockId) {
    EXPECT_COMPILE_ERROR(TestThreadIdOutsideBlockId, MatchesPattern(R"(GPU thread loop over f\d+\.s\d+\.v\d+\.v\d+ must be inside a GPU block loop\. The loop nest is:\ncompute_at for f\d+:\n gpu_thread f\d+\.s\d+\.v\d+\.v\d+:)"));
}
