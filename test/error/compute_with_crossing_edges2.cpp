#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestComputeWithCrossingEdges2() {
    Var x("x"), y("y");
    Func f("f"), g("g");

    f(x, y) = x + y;
    f(x, y) += 1;

    g(x, y) = x - y;
    g(x, y) += 1;

    f.compute_root();
    g.compute_root();

    f.compute_with(g.update(0), y);
    f.update(0).compute_with(g, y);

    Pipeline p({f, g});
    p.realize({200, 200});
}
}  // namespace

TEST(ErrorTests, ComputeWithCrossingEdges2) {
    EXPECT_COMPILE_ERROR(TestComputeWithCrossingEdges2, MatchesPattern(R"(Invalid compute_with: impossible to establish correct stage order between f(\$\d+)?\.s\d+ with g(\$\d+)?\.s\d+ and f(\$\d+)?\.s\d+ with g(\$\d+)?\.s\d+)"));
}
