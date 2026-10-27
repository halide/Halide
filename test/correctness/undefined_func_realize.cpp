#include "Halide.h"
#include "expect_user_error.h"
#include "halide_test_dirs.h"

#include <cstdio>

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("undefined_func_realize", "Can't realize undefined Func.", []() {
        Func f("f");

        Buffer<int32_t> result = f.realize({100, 5, 3});
        (void)result;
    });
}
