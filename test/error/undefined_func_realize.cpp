#include "Halide.h"
#include "halide_test_dirs.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestUndefinedFuncRealize() {
    Func f("f");

    Buffer<int32_t> result = f.realize({100, 5, 3});

    // We shouldn't reach here, because there should have been a compile error.
}
}  // namespace

TEST(ErrorTests, UndefinedFuncRealize) {
    EXPECT_COMPILE_ERROR(TestUndefinedFuncRealize, MatchesPattern(R"(Can't realize undefined Func\.)"));
}
