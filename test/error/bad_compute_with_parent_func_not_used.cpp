#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestBadComputeWithParentFuncNotUsed() {
    Var x("x"), y("y");
    Func f("f"), g("g"), h("h"), p("p");

    f(x, y) = x + y;
    g(x, y) = x - y;
    p(x, y) = x * y;
    h(x, y) = g(x + 2, y - 2) + p(x, y);
    h(x, y) += f(x - 1, y + 1);

    f.compute_at(h, y);
    g.compute_at(h, y);
    p.compute_at(h, y);

    p.compute_with(f, x);
    g.compute_with(f, x);
    h.realize({200, 200});
}
}  // namespace

TEST(ErrorTests, BadComputeWithParentFuncNotUsed) {
    EXPECT_COMPILE_ERROR(TestBadComputeWithParentFuncNotUsed, HasSubstr("TODO"));
}
