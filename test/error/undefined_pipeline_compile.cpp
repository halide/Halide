#include "Halide.h"
#include "halide_test_dirs.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestUndefinedPipelineCompile() {
    Func f("f");

    Pipeline p(f);
    std::string test_object = Internal::get_test_tmp_dir() + "compile_undefined.o";
    p.compile_to_object(test_object, {}, "f");

    // We shouldn't reach here, because there should have been a compile error.
}
}  // namespace

TEST(ErrorTests, UndefinedPipelineCompile) {
    EXPECT_COMPILE_ERROR(TestUndefinedPipelineCompile, MatchesPattern(R"(Can't compile Pipeline with undefined output Func: f(\$\d+)?\.)"));
}
