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
    EXPECT_COMPILE_ERROR(TestBadPartitionAlways, HasSubstr("TODO"));
}
