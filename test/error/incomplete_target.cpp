#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestIncompleteTarget() {
    Target t("debug");
}
}  // namespace

TEST(ErrorTests, IncompleteTarget) {
    EXPECT_COMPILE_ERROR(
        TestIncompleteTarget,
        HasSubstr("Did not understand Halide target debug"));
}
