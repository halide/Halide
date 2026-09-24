#include "Halide.h"
#include "expect_user_error.h"
#include "halide_test_dirs.h"

#include <cstdio>

using namespace Halide;

int main(int argc, char **argv) {
    std::string test_object = Internal::get_test_tmp_dir() + "compile_undefined.o";
    return error_test("undefined_func_compile", "Can't compile Pipeline with undefined output Func: f.", []() {
        Func f("f");

        f.compile_to_object(test_object, {}, "f");
    });
}
