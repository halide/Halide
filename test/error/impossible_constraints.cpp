#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestImpossibleConstraints() {
    ImageParam input(Float(32), 2, "in");

    Func out("out");

    // The requires that the input be larger than the input
    out() = input(input.width(), input.height()) + input(0, 0);

    out.infer_input_bounds({});
}
}  // namespace

TEST(ErrorTests, ImpossibleConstraints) {
    EXPECT_COMPILE_ERROR(
        TestImpossibleConstraints,
        MatchesPattern(R"(Inferring input bounds on Pipeline didn't converge )"
                       R"(after 16 iterations\. There may be unsatisfiable )"
                       R"(constraints)"));
}
