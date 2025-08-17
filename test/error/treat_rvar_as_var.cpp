#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestTreatRvarAsVar() {
    Func f;
    Var x, y;

    RDom r(0, 10);
    f(x, y) += r;

    // Sneakily disguising an RVar as a Var by reusing the name should result in
    // an error. Otherwise it can permit schedules that aren't legal.
    Var xo, xi;
    f.update().split(Var(r.x.name()), xo, xi, 8, TailStrategy::RoundUp);
}
}  // namespace

TEST(ErrorTests, TreatRvarAsVar) {
    EXPECT_COMPILE_ERROR(
        TestTreatRvarAsVar,
        MatchesPattern(R"(Var r\d+\$x used in scheduling directive has the same name )"
                       R"(as existing RVar r\d+\$x)"));
}
