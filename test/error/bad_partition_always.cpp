#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestBadPartitionAlways() {
    Func f("f");
    Var x("x");

    f(x) = 0;
    f.partition(x, Partition::Always);

    f.realize({10});
}
}  // namespace

TEST(ErrorTests, BadPartitionAlways) {
    EXPECT_COMPILE_ERROR(
        TestBadPartitionAlways,
        MatchesPattern(R"(Loop Partition Policy is set to Always for )"
                       R"(f(\$\d+)?\.s\d+\.x, but no loop partitioning )"
                       R"(was performed\.)"));
}
