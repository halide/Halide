#include <assert.h>
#include <stdio.h>
#include "Halide.h"

#include "test/common/halide_test_dirs.h"

using namespace Halide;

int main(int argc, char **argv) {
    Func f("f");

    std::string test_object = Internal::get_test_tmp_dir() + "compile_undefined.o";
    f.compile_to_object(test_object, {}, "f");

    // We shouldn't reach here, because there should have been a compile error.
    printf("There should have been an error\n");

    return 0;
}
