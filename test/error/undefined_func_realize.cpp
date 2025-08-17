#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

TEST(ErrorTests, UndefinedFuncRealize) {
    EXPECT_COMPILE_ERROR(
        [] {
            Func f("f");
            Buffer<int32_t> result = f.realize({100, 5, 3});
        },
        HasSubstr("Can't realize undefined Func."));
}
