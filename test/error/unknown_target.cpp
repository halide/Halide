#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestUnknownTarget() {
    Target t;

    // Calling natural_vector_size() on a Target with Unknown fields
    // should generate user_error.
    (void)t.natural_vector_size<float>();
}
}  // namespace

TEST(ErrorTests, UnknownTarget) {
    EXPECT_COMPILE_ERROR(
        TestUnknownTarget,
        HasSubstr("natural_vector_size cannot be used on a Target with "
                  "Unknown values."));
}
