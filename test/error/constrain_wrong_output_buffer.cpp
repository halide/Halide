#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestConstrainWrongOutputBuffer() {
    Func f;
    Var x;
    f(x) = Tuple(x, sin(x));

    // Don't do this. Instead constrain the size of output buffer 0.
    f.output_buffers()[1].dim(0).set_min(4);

    f.compile_jit();
}
}  // namespace

TEST(ErrorTests, ConstrainWrongOutputBuffer) {
    EXPECT_COMPILE_ERROR(
        TestConstrainWrongOutputBuffer,
        HasSubstr("Can't constrain the min or extent of an output buffer beyond "
                  "the first. They are implicitly constrained to have the same "
                  "min and extent as the first output buffer."));
}
