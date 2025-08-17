#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestRunWithLargeStackThrows() {
    Internal::run_with_large_stack([] {
#ifdef HALIDE_WITH_EXCEPTIONS
        throw RuntimeError("Error from run_with_large_stack");
#else
        _halide_user_assert(0) << "Error from run_with_large_stack (no exceptions)";
#endif
    });
}
}  // namespace

TEST(ErrorTests, RunWithLargeStackThrows) {
    EXPECT_RUNTIME_ERROR(TestRunWithLargeStackThrows,
                         HasSubstr("Error from run_with_large_stack"));
}
