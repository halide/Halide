// https://github.com/halide/Halide/issues/6808
#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestRdomWhereRaces() {
    Func f;
    Var x;

    RDom r(0, 10);
    f(x) = 1;
    r.where(f(0) == 1);
    f(r) = 2;

    f.update().parallel(r);
}
}  // namespace

TEST(ErrorTests, RdomWhereRaces) {
    EXPECT_COMPILE_ERROR(TestRdomWhereRaces, HasSubstr("TODO"));
}
