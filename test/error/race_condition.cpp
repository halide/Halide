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

    // This schedule should be forbidden, because it causes a race condition.
    f.update().parallel(r.y);

    // We shouldn't reach here, because there should have been a compile error.
}
}  // namespace

TEST(ErrorTests, RaceCondition) {
    EXPECT_COMPILE_ERROR(TestRaceCondition, MatchesPattern(R"(In schedule for f\d+\.update\(0\), marking var r\d+\$y as parallel or vectorized may introduce a race condition resulting in incorrect output\. It is possible to parallelize this by using the atomic\(\) method if the operation is associative, or set override_associativity_test to true in the atomic method if you are certain that the operation is associative\. It is also possible to override this error using the allow_race_conditions\(\) method\. Use allow_race_conditions\(\) with great caution, and only when you are willing to accept non-deterministic output, or you can prove that any race conditions in this code do not change the output, or you can prove that there are actually no race conditions, and that Halide is being too cautious\.)"));
}
