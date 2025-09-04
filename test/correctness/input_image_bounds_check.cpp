#include "Halide.h"
#include <gtest/gtest.h>

// This tests out-of-bounds reads from an input image
// TODO: move this to error/ ?

using namespace Halide;

namespace {
// Custom error handler. If we don't define this, it'll just print out
// an error message and quit
struct InputImageBoundsCheckContext : JITUserContext {
    bool error_occurred{false};
    InputImageBoundsCheckContext() {
        handlers.custom_error = custom_error;
    }
    static void custom_error(JITUserContext *ctx, const char *msg) {
        auto *self = static_cast<InputImageBoundsCheckContext *>(ctx);
        self->error_occurred = true;
    }
};
}  // namespace

TEST(InputImageBoundsCheckTest, BasicOutOfBounds) {
    Buffer<float> input(19);
    for (int i = 0; i < 19; i++) {
        input(i) = i;
    }
    Var x;
    Func f;
    f(x) = input(x) * 2;

    // One easy way to read out of bounds
    InputImageBoundsCheckContext ctx;
    f.realize(&ctx, {23});
    EXPECT_TRUE(ctx.error_occurred) << "There should have been an out-of-bounds error";
}

TEST(InputImageBoundsCheckTest, VectorizationBounds) {
    Buffer<float> input(19);
    for (int i = 0; i < 19; i++) {
        input(i) = i;
    }
    Var x;

    // Another more subtle way to read out of bounds used to be due to
    // bounds expansion when vectorizing. This used to be an
    // out-of-bounds error, but now isn't! Hooray!
    Func g, h;
    g(x) = input(x) * 2;
    h(x) = g(x);
    g.compute_root().vectorize(x, 4);

    InputImageBoundsCheckContext ctx;
    h.realize(&ctx, {18});
    EXPECT_FALSE(ctx.error_occurred) << "There should not have been an out-of-bounds error";
}

TEST(InputImageBoundsCheckTest, SmallInputVectorization) {
    Var x;

    // But if we try to make the input smaller than the vector width, it
    // still won't work.
    Buffer<float> small_input(3);
    Func i;
    i(x) = small_input(x);
    i.vectorize(x, 4);

    InputImageBoundsCheckContext ctx;
    i.realize(&ctx, {4});
    EXPECT_TRUE(ctx.error_occurred) << "There should have been an out-of-bounds error";
}
