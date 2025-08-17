#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestBadFuncObject() {
    const Internal::Function func{};
    const Func f{func};  // internal_assert

    std::cout << f.name() << "\n";  // segfaults without above assert
}
}  // namespace

TEST(ErrorTests, BadFuncObject) {
    EXPECT_INTERNAL_ERROR(
        TestBadFuncObject,
        HasSubstr("Can't construct Func from undefined Function"));
}
