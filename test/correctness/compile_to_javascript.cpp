#include <Halide.h>
#include <stdio.h>

#include "test/common/halide_test_dirs.h"

using namespace Halide;

void compile_javascript(Func j) {
    std::string object_name = Internal::get_test_tmp_dir() + "javascript.js";
    printf("Compiling to: %s\n", object_name.c_str());

    Internal::ensure_no_file_exists(object_name);

    std::vector<Argument> empty_args;
    j.compile_to_javascript(object_name, empty_args, "");
    j.compile_to_c(object_name+".cpp", empty_args, "");

    Internal::assert_file_exists(object_name);
}

int main(int argc, char **argv) {
    Buffer<float> im(3, 3, "im");
    im.for_each_element([&im](int x, int y) {
        im(x, y) = x + y;
    });

    Func f, g, h, j;
    Var x, y;
    f(x, y) = x + y;
    g(x, y) = cast<float>(f(x, y) + f(x+1, y) + im(x, y));
    h(x, y) = f(x, y) + g(x, y);
    j(x, y) = h(x, y) * 2;

    f.compute_root();
    g.compute_root();
    h.compute_root();

    compile_javascript(j);

    printf("Success!\n");
    return 0;
}
