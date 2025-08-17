#include "Halide.h"
#include "halide_test_dirs.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestUndefinedFuncCompile() {
    Func f("f");

    std::string test_object = Internal::get_test_tmp_dir() + "compile_undefined.o";
    f.compile_to_object(test_object, {}, "f");
}
}  // namespace

TEST(ErrorTests, UndefinedFuncCompile) {
    EXPECT_COMPILE_ERROR(
        TestUndefinedFuncCompile,
        MatchesPattern(R"(Can't compile Pipeline with undefined output )"
                       R"(Func: f(\$\d+)?\.)"));
}
