#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestAmbiguousInlineReductions() {
    Func f("f");
    Var x("x"), y("y");
    RDom r1(0, 10, "r1"), r2(0, 10, "r2"), r3(0, 10, "r3");

    f(x, y) = product(sum(r1, r1 + r3) + sum(r2, r2 * 2 + r3));

    // Is this the product over r1, or r3? It must be r3 because r1 is
    // used on the LHS, but Halide's not smart enough to know
    // that. All it sees is a product over an expression with two
    // reduction domains.
    f(r1, y) += product(sum(r2, r1 + r2 + r3));

    Buffer<int> result = f.realize({10, 10});
}
}  // namespace

TEST(ErrorTests, AmbiguousInlineReductions) {
    EXPECT_COMPILE_ERROR(
        TestAmbiguousInlineReductions,
        MatchesPattern(R"(Inline reduction \"product(\$\d+)?\" refers )"
                       R"(to reduction variables from multiple reduction )"
                       R"(domains: r\d+\$x, r\d+\$x)"));
}
