#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("missing_args", "which was not found in the argument list.", []() {
        Func f;
        Var x;
        ImageParam im(Int(8), 2);
        Param<float> arg;

        f(x) = im(x, x) + arg;

        std::vector<Argument> args;
        // args.push_back(im);
        // args.push_back(arg);
        f.compile_to_object("f.o", args, "f");
    });
}
