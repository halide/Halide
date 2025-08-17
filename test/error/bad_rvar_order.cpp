#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestBadRvarOrder() {
    RDom r1(0, 10, 0, 10);

    Func f("f");
    Var x, y;
    f(x, y) = x + y;
    f(r1.x, r1.y) += f(r1.y, r1.x);

    // It's not permitted to change the relative ordering of reduction
    // domain variables when it could change the meaning.
    f.update().reorder(r1.y, r1.x);

    f.realize({10, 10});
}
}  // namespace

TEST(ErrorTests, BadRvarOrder) {
    EXPECT_COMPILE_ERROR(TestBadRvarOrder, HasSubstr("TODO"));
}
