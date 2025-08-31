#include "Halide.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace Halide;

namespace {
struct ExternErrorContext : JITUserContext {
    bool extern_error_called{false};
    bool error_occurred{false};
};

extern "C" HALIDE_EXPORT_SYMBOL int extern_error(JITUserContext *ctx, halide_buffer_t *) {
    static_cast<ExternErrorContext *>(ctx)->extern_error_called = true;
    return halide_error_code_generic_error;
}

extern "C" HALIDE_EXPORT_SYMBOL void my_halide_error(JITUserContext *ctx, const char *msg) {
    static_cast<ExternErrorContext *>(ctx)->error_occurred = true;
}
}  // namespace

TEST(ExternErrorTest, Basic) {
    if (get_jit_target_from_environment().arch == Target::WebAssembly) {
        GTEST_SKIP() << "WebAssembly JIT does not support passing arbitrary pointers to/from HalideExtern code.";
    }

    Func f;
    f.define_extern("extern_error", {user_context_value()}, Float(32), 1);
    f.jit_handlers().custom_error = my_halide_error;

    ExternErrorContext ctx;
    f.realize(&ctx, {100});

    EXPECT_TRUE(ctx.extern_error_called) << "extern_error was not called";
    EXPECT_TRUE(ctx.error_occurred) << "There was supposed to be an error";
}
