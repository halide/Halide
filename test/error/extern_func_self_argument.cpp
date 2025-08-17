#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

extern "C" int extern_func() {
    return 0;
}

namespace {
void TestExternFuncSelfArgument() {
    Func f("f");

    f.define_extern("extern_func", {f}, Int(32), 2);
    f.infer_arguments();
}
}  // namespace

TEST(ErrorTests, ExternFuncSelfArgument) {
    EXPECT_COMPILE_ERROR(TestExternFuncSelfArgument, MatchesPattern(R"(Extern Func has itself as an argument)"));
}
