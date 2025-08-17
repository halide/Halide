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
    EXPECT_COMPILE_ERROR(
        TestRdomWhereRaces,
        MatchesPattern(
            R"(In schedule for f\d+\.update\(0\), marking var r\d+\$x as )"
            R"(parallel or vectorized may introduce a race condition )"
            R"(resulting in incorrect output\. It is possible to )"
            R"(parallelize this by using the atomic\(\) method if )"
            R"(the operation is associative, or set override_associativity_test )"
            R"(to true in the atomic method if you are certain that the operation )"
            R"(is associative\. It is also possible to override this error using the )"
            R"(allow_race_conditions\(\) method\. Use allow_race_conditions\(\) with )"
            R"(great caution, and only when you are willing to accept )"
            R"(non-deterministic output, or you can prove that any race conditions )"
            R"(in this code do not change the output, or you can prove that there )"
            R"(are actually no race conditions, and that Halide is being too )"
            R"(cautious\.)"));
}
