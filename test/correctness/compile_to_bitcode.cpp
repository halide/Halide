#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Func f, g, h, j;
    Var x, y;
    f(x, y) = x + y;
    g(x, y) = cast<float>(f(x, y) + f(x+1, y));
    h(x, y) = f(x, y) + g(x, y);
    j(x, y) = h(x, y) * 2;

    f.compute_root();
    g.compute_root();
    h.compute_root();

    const char *result_file = "compile_to_bitcode.bc";

    Internal::file_unlink_or_die(result_file);

    std::vector<Argument> empty_args;
    j.compile_to_bitcode(result_file, empty_args);

    Internal::file_exists_or_die(result_file);

    printf("Success!\n");
    return 0;
}
