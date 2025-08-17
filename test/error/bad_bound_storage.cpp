#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestBadBoundStorage() {
    Func f("f"), g("g");
    Var x("x"), y("y");

    f(x, y) = x + y;
    g(x, y) = f(x, y) * 2;

    f.compute_at(g, y);
    f.bound_storage(x, 9);
    g.realize({10, 10});
}
}  // namespace

TEST(ErrorTests, BadBoundStorage) {
    EXPECT_RUNTIME_ERROR(
        TestBadBoundStorage,
        MatchesPattern(R"(The explicit allocation bound \(9\) of dimension )"
                       R"(x of f(\$\d+)? is too small to store the required )"
                       R"(region \(10\)\.)"));
}
