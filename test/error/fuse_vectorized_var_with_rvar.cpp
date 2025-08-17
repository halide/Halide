#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {

// From https://github.com/halide/Halide/issues/7871

void TestFuseVectorizedVarWithRvar() {
    Func input("input");
    Func local_sum("local_sum");
    Func blurry("blurry");
    Var x("x"), y("y");
    RVar yryf;
    input(x, y) = 2 * x + 5 * y;
    RDom r(-2, 5, -2, 5, "rdom_r");
    local_sum(x, y) = 0;
    local_sum(x, y) += input(x + r.x, y + r.y);
    blurry(x, y) = cast<int32_t>(local_sum(x, y) / 25);

    // Should throw an error because we're trying to fuse a vectorized Var with
    // an impure RVar.
    local_sum.update(0).vectorize(y).fuse(y, r.y, yryf);
}
}  // namespace

TEST(ErrorTests, FuseVectorizedVarWithRvar) {
    EXPECT_COMPILE_ERROR(
        TestFuseVectorizedVarWithRvar,
        MatchesPattern(R"(In schedule for local_sum(\$\d+)?\.update\(0\), marking )"
                       R"(var r\d+ as parallel or vectorized may introduce a race )"
                       R"(condition resulting in incorrect output\. It is possible )"
                       R"(to parallelize this by using the atomic\(\) method if the )"
                       R"(operation is associative, or set override_associativity_test )"
                       R"(to true in the atomic method if you are certain that the )"
                       R"(operation is associative\. It is also possible to override )"
                       R"(this error using the allow_race_conditions\(\) method\. )"
                       R"(Use allow_race_conditions\(\) with great caution, and only )"
                       R"(when you are willing to accept non-deterministic output, or )"
                       R"(you can prove that any race conditions in this code do not )"
                       R"(change the output, or you can prove that there are actually )"
                       R"(no race conditions, and that Halide is being too cautious\.)"));
}
