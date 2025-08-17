#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestComputeWithCrossingEdges1() {
    Var x("x"), y("y");
    Func f("f"), g("g");

    f(x, y) = x + y;
    f(x, y) += 1;
    f(x, y) += 1;

    g(x, y) = x - y;

    f.compute_root();
    g.compute_root();

    f.compute_with(g, y);
    f.update(1).compute_with(g, y);

    Pipeline p({f, g});
    p.realize({200, 200});
}
}  // namespace

TEST(ErrorTests, ComputeWithCrossingEdges1) {
    EXPECT_COMPILE_ERROR(TestComputeWithCrossingEdges1, MatchesPattern(R"(Invalid compute_with: impossible to establish correct stage order between f(\$\d+)?\.s\d+ with g(\$\d+)?\.s\d+ and f(\$\d+)?\.s\d+ with g(\$\d+)?\.s\d+)"));
}
