#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestDefineAfterRealize() {
    Func f, g;
    Var x;

    f(x) = x;

    Buffer<int> im = f.realize({10});

    // Now try to add an update definition to f
    f(x) += 1;
}
}  // namespace

TEST(ErrorTests, DefineAfterRealize) {
    EXPECT_COMPILE_ERROR(TestDefineAfterRealize, HasSubstr("TODO"));
}
