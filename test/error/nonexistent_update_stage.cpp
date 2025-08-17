#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestNonexistentUpdateStage() {
    Func f;
    Var x;
    f(x) = x;
    f.update().vectorize(x, 4);
}
}  // namespace

TEST(ErrorTests, NonexistentUpdateStage) {
    EXPECT_COMPILE_ERROR(TestNonexistentUpdateStage, MatchesPattern(R"(Call to update with index larger than last defined update stage for Func \"f\d+\"\.)"));
}
