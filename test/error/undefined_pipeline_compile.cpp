#include "Halide.h"
#include "halide_test_dirs.h"

#include <cstdio>

using namespace Halide;

int main(int argc, char **argv) {
    Func f("f");

    Pipeline p(f);
    std::string test_object = Internal::get_test_tmp_dir() + "compile_undefined.o";
    p.compile_to_object(test_object, {}, "f");

    // We shouldn't reach here, because there should have been a compile error.
    printf("Success!\n");
    return 0;
}
