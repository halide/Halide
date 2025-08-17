#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestBadPrefetch() {
    Func f("f"), g("g");
    Var x("x"), y("y");

    f(x, y) = x + y;
    g(x, y) = f(0, 0);

    f.compute_root();
    g.prefetch(f, y, x, 8);
    g.print_loop_nest();

    Module m = g.compile_to_module({});
}
}  // namespace

TEST(ErrorTests, BadPrefetch) {
    EXPECT_COMPILE_ERROR(TestBadPrefetch, HasSubstr("TODO"));
}
