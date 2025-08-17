#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestForwardOnUndefinedBuffer() {
    const Buffer<> foo;
    foo.raw_buffer();
}
}  // namespace

TEST(ErrorTests, ForwardOnUndefinedBuffer) {
    EXPECT_COMPILE_ERROR(
        TestForwardOnUndefinedBuffer,
        HasSubstr("Undefined buffer calling const method raw_buffer"));
}
