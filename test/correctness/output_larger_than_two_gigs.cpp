#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

namespace {
bool error_occurred = false;
void halide_error(JITUserContext *ctx, const char *msg) {
    printf("Expected: %s\n", msg);
    error_occurred = true;
}
}  // namespace

TEST(OutputLargerThanTwoGigsTest, OutputLargerThanTwoGigs) {
    error_occurred = false;

    Var x, y, z;
    Func identity_uint8;
    identity_uint8(x, y, z) = cast<uint8_t>(42);

    uint8_t c[4096];
    memset(c, 99, sizeof(c));

    halide_dimension_t shape[] = {{0, 4096, 1},
                                  {0, 4096, 0},
                                  {0, 256, 0}};
    Buffer<uint8_t> output(c, 3, shape);

    identity_uint8.jit_handlers().custom_error = halide_error;

    Target t = get_jit_target_from_environment();

    if (t.bits != 32) {
        EXPECT_NO_THROW(identity_uint8.compile_jit(t.with_feature(Target::LargeBuffers)));
        EXPECT_NO_THROW(identity_uint8.realize(output));
        EXPECT_FALSE(error_occurred);

        EXPECT_EQ(output(0, 0, 0), 42);
        EXPECT_EQ(output(output.extent(0) - 1, output.extent(1) - 1, output.extent(2) - 1), 42);
    }

    EXPECT_NO_THROW(identity_uint8.compile_jit(t));
    EXPECT_NO_THROW(identity_uint8.realize(output));
    EXPECT_TRUE(error_occurred);
}
