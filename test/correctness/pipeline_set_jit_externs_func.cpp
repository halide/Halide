#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

namespace {
int call_counter = 0;
extern "C" HALIDE_EXPORT_SYMBOL float pipeline_set_jit_externs_func(int x, float y) {
    call_counter++;
    return x * y;
}
HalideExtern_2(float, pipeline_set_jit_externs_func, int, float);
}  // namespace

TEST(PipelineSetJitExternsFuncTest, PipelineSetJitExternsFunc) {
    // set_jit_externs() implicitly adds a user_context arg to the externs, which
    // we can't yet support. TODO: this actually should work, but doesn't.
    if (get_jit_target_from_environment().arch == Target::WebAssembly) {
        GTEST_SKIP() << "WebAssembly JIT does not support passing arbitrary pointers to/from HalideExtern code.";
    }

    call_counter = 0;

    std::vector<ExternFuncArgument> args;
    args.emplace_back(user_context_value());

    Var x, y;
    Func monitor;
    monitor(x, y) = pipeline_set_jit_externs_func(x, cast<float>(y));

    Func f;
    f.define_extern("extern_func", args, Float(32), 2);

    Pipeline p(f);
    p.set_jit_externs({{"extern_func", JITExtern{monitor}}});

    Buffer<float> imf;
    ASSERT_NO_THROW(imf = p.realize({32, 32}));

    for (int i = 0; i < 32; i++) {
        for (int j = 0; j < 32; j++) {
            float correct = (float)(i * j);
            EXPECT_NEAR(imf(i, j), correct, 0.001f) << "imf[" << i << ", " << j << "]";
        }
    }

    EXPECT_EQ(call_counter, 32 * 32)
        << "my_func was called " << call_counter << " times instead of " << (32 * 32);
}
