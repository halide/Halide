#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestFiveDGpuBuffer() {
    // Move this test to correctness once we can support >4d buffer_ts on the gpu
    Func f;
    Var v0, v1, v2, v3, v4;

    f(v0, v1, v2, v3, v4) = v0 + 2 * v1 + 4 * v2 + 8 * v3 + 16 * v4;

    f.compute_root().gpu_blocks(v3, v4).gpu_threads(v1, v2);

    // Linearize into an output buffer
    Func g;
    g(v0) = f(v0 % 2, (v0 / 2) % 2, (v0 / 4) % 2, (v0 / 8) % 2, (v0 / 16) % 2);

    Buffer<int> result = g.realize({32});
}
}  // namespace

TEST(ErrorTests, FiveDGpuBuffer) {
    if (!get_jit_target_from_environment().has_gpu_feature()) {
        GTEST_SKIP() << "No GPU target enabled.";
    }
    EXPECT_COMPILE_ERROR(TestFiveDGpuBuffer, HasSubstr("TODO"));
}
