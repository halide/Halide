#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestInitDefShouldBeAllVars() {
    Buffer<int> in(10, 10);

    Func f("f");
    RDom r(0, in.width(), 0, in.height());
    f(r.x, r.y) = in(r.x, r.y) + 2;
    f.realize({in.width(), in.height()});
}
}  // namespace

TEST(ErrorTests, InitDefShouldBeAllVars) {
    EXPECT_COMPILE_ERROR(TestInitDefShouldBeAllVars, HasSubstr("TODO"));
}
