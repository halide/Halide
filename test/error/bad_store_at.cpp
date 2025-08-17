#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestBadStoreAt() {
    Func f("f"), g("g"), h("h");
    Var x("x"), y("y");

    f(x) = x;
    g(x) = f(x);
    h(x, y) = g(x);

    g.compute_at(h, y);

    // This makes no sense, because the compute_at level is higher than the store_at level
    f.store_at(h, y).compute_root();

    h.realize({10, 10});
}
}  // namespace

TEST(ErrorTests, BadStoreAt) {
    EXPECT_COMPILE_ERROR(TestBadStoreAt, HasSubstr("TODO"));
}
