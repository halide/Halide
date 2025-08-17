#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestRaceCondition() {
    Func f, g;
    Var x, y;

    f(x, y) = 0;

    RDom r(0, 10, 0, 10);
    f(r.x, r.y) += f(r.y, r.x);

    // This schedule should be forbidden because it causes a race condition.
    f.update().parallel(r.y);
}
}  // namespace

TEST(ErrorTests, RaceCondition) {
    EXPECT_COMPILE_ERROR(
        TestRaceCondition,
        MatchesPattern(R"(In schedule for f\d+\.update\(0\), marking var r\d+\$y )"
                       R"(as parallel or vectorized may introduce a race condition )"
                       R"(resulting in incorrect output\. It is possible to )"
                       R"(parallelize this by using the atomic\(\) method if the )"
                       R"(operation is associative, or set override_associativity_test )"
                       R"(to true in the atomic method if you are certain that the )"
                       R"(operation is associative\. It is also possible to override )"
                       R"(this error using the allow_race_conditions\(\) method\. Use )"
                       R"(allow_race_conditions\(\) with great caution, and only )"
                       R"(when you are willing to accept non-deterministic output, )"
                       R"(or you can prove that any race conditions in this code do )"
                       R"(not change the output, or you can prove that there are )"
                       R"(actually no race conditions, and that Halide is being )"
                       R"(too cautious\.)"));
}
