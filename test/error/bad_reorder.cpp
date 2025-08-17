#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestBadReorder() {
    Var x, y, xi;

    Func f;

    f(x, y) = x;

    f
        .split(x, x, xi, 8)
        .reorder(x, y, x);

    // Oops, probably meant "xi" rather than x in the reorder call
}
}  // namespace

TEST(ErrorTests, BadReorder) {
    EXPECT_COMPILE_ERROR(
        TestBadReorder,
        MatchesPattern(R"(In schedule for f\d+, call to )"
                       R"(reorder references v\d+ twice\.)"));
}
