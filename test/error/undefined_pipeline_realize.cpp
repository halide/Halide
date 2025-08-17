#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestUndefinedPipelineRealize() {
    Func f("f");

    Pipeline p(f);
    Buffer<int32_t> result = p.realize({100, 5, 3});

    // We shouldn't reach here, because there should have been a compile error.
}
}  // namespace

TEST(ErrorTests, UndefinedPipelineRealize) {
    EXPECT_COMPILE_ERROR(TestUndefinedPipelineRealize, MatchesPattern(R"(Func f(\$\d+)? is defined with 0 dimensions, but realize\(\) is requesting a realization with 3 dimensions\.)"));
}
