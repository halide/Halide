#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestRdomUndefined() {
    Expr undef_min, undef_extent;

    // This should assert-fail
    RDom r(undef_min, undef_min);

    // Just to ensure compiler doesn't optimize-away the RDom ctor
    printf("Dimensions: %d\n", r.dimensions());
}
}  // namespace

TEST(ErrorTests, RdomUndefined) {
    EXPECT_COMPILE_ERROR(TestRdomUndefined, HasSubstr("TODO"));
}
