#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;
using namespace Halide::Internal;

namespace {
void TestWrapperNeverUsed() {
    Var x("x"), y("y");
    Func f("f"), g("g"), h("h");
    f(x, y) = x + y;
    g(x, y) = 5;
    h(x, y) = f(x, y) + g(x, y);

    f.compute_root();
    f.in(g).compute_root();

    // This should cause an error since f.in(g) was called but 'f' is
    // never used in 'g'.
    h.realize({5, 5});
}
}  // namespace

TEST(ErrorTests, WrapperNeverUsed) {
    EXPECT_COMPILE_ERROR(
        TestWrapperNeverUsed,
        MatchesPattern(R"(Cannot wrap \"f(\$\d+)?\" in \"g(\$\d+)?\" because )"
                       R"(\"g(\$\d+)?\" does not call \"f(\$\d+)?\"\n)"
                       R"(Direct callees of \"g(\$\d+)?\" are:)"));
}
