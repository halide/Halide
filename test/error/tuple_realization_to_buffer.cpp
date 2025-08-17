#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestTupleRealizationToBuffer() {
    Func f;
    Var x;

    f(x) = {x, x, x};

    Buffer<int> buf = f.realize({1024});
}
}  // namespace

TEST(ErrorTests, TupleRealizationToBuffer) {
    EXPECT_COMPILE_ERROR(
        TestTupleRealizationToBuffer,
        HasSubstr("Cannot cast Realization with 3 elements to a Buffer"));
}
