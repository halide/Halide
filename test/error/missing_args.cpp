#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestMissingArgs() {
    Func f;
    Var x;
    ImageParam im(Int(8), 2);
    Param<float> arg;

    f(x) = im(x, x) + arg;

    std::vector<Argument> args;
    // args.push_back(im);
    // args.push_back(arg);
    f.compile_to_object("f.o", args, "f");
}
}  // namespace

TEST(ErrorTests, MissingArgs) {
    EXPECT_COMPILE_ERROR(TestMissingArgs, HasSubstr("TODO"));
}
