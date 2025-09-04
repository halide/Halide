#include "Halide.h"
#include <gtest/gtest.h>
#include <memory>

using namespace Halide;

namespace {

int error_occurred = false;
void halide_error(JITUserContext *ctx, const char *msg) {
    error_occurred = true;
}

}  // namespace

TEST(InputLargerThanTwoGigsTest, LargeBufferHandling) {
    uint8_t c[4096];
    memset(c, 42, sizeof(c));

    halide_dimension_t shape[] = {{0, 4096, 1},
                                  {0, 4096, 0},
                                  {0, 256, 0}};
    Halide::Buffer<uint8_t> buf(c, 3, shape);

    ImageParam input(UInt(8), 3);
    input.set(buf);

    Var x;
    Func grand_total;
    grand_total() = cast<uint64_t>(input(0, 0, 0) + input(input.dim(0).extent() - 1, input.dim(1).extent() - 1, input.dim(2).extent() - 1));
    grand_total.jit_handlers().custom_error = halide_error;

    Target t = get_jit_target_from_environment();

    Buffer<uint64_t> result;
    if (t.bits != 32) {
        grand_total.compile_jit(t.with_feature(Target::LargeBuffers));
        ASSERT_NO_THROW(result = grand_total.realize());
        EXPECT_FALSE(error_occurred);
        EXPECT_EQ(result(0), 84);
    }

    ASSERT_NO_THROW(grand_total.compile_jit(t));
    ASSERT_NO_THROW(grand_total.realize(result));
    EXPECT_TRUE(error_occurred);
}
