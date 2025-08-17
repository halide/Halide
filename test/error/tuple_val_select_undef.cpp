#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;
using namespace Halide::Internal;

namespace {
void TestTupleValSelectUndef() {
    Var x("x");
    Func f("f");

    // Should result in an error
    f(x) = {x, select(x < 20, 20 * x, undef<int>())};
    f.realize({10});
}
}  // namespace

TEST(ErrorTests, TupleValSelectUndef) {
    EXPECT_COMPILE_ERROR(TestTupleValSelectUndef, HasSubstr("TODO"));
}
