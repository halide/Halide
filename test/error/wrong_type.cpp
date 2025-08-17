#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

#ifdef NDEBUG
#error "wrong_type requires assertions"
#endif

namespace {
void TestWrongType() {
    Func f;
    Var x;
    f(x) = x;
    Buffer<float> im = f.realize({100});
}
}  // namespace

TEST(ErrorTests, WrongType) {
    EXPECT_COMPILE_ERROR(TestWrongType, HasSubstr("TODO"));
}
