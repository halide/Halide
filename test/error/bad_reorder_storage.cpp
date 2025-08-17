#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestBadReorderStorage() {
    Var x, y, xi;

    Func f;

    f(x, y) = x;

    f.reorder_storage(x, y, x);
}
}  // namespace

TEST(ErrorTests, BadReorderStorage) {
    EXPECT_COMPILE_ERROR(TestBadReorderStorage, HasSubstr("TODO"));
}
