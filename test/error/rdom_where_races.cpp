// https://github.com/halide/Halide/issues/6808
#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestRdomWhereRaces() {
    Func f;
    Var x;

    RDom r(0, 10);
    f(x) = 1;
    r.where(f(0) == 1);
    f(r) = 2;

    f.update().parallel(r);
}
}  // namespace

TEST(ErrorTests, RdomWhereRaces) {
    EXPECT_COMPILE_ERROR(TestRdomWhereRaces, MatchesPattern(R"(In schedule for f\d+\.update\(0\), marking var r\d+\$x as parallel or vectorized may introduce a race condition resulting in incorrect output\. It is possible to parallelize this by using the atomic\(\) method if the operation is associative, or set override_associativity_test to true in the atomic method if you are certain that the operation is associative\. It is also possible to override this error using the allow_race_conditions\(\) method\. Use allow_race_conditions\(\) with great caution, and only when you are willing to accept non-deterministic output, or you can prove that any race conditions in this code do not change the output, or you can prove that there are actually no race conditions, and that Halide is being too cautious\.)"));
}
