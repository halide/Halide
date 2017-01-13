#include "Halide.h"
#include <stdio.h>

using namespace Halide;

void testCompileToOutput(Func j) {
    const char *fn_object = "compile_to_native.o";

    Internal::ensure_no_file_exists(fn_object);

    std::vector<Argument> empty_args;
    j.compile_to(Outputs().object(fn_object), empty_args, "");

    Internal::assert_file_exists(fn_object);
}

void testCompileToOutputAndAssembly(Func j) {
    const char *fn_object = "compile_to_native1.o";
    const char *fn_assembly = "compile_to_assembly1.s";

    Internal::ensure_no_file_exists(fn_object);
    Internal::ensure_no_file_exists(fn_assembly);

    std::vector<Argument> empty_args;
    j.compile_to(Outputs().object(fn_object).assembly(fn_assembly), empty_args, "");

    Internal::assert_file_exists(fn_object);
    Internal::assert_file_exists(fn_assembly);
}

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

    testCompileToOutput(j);

    testCompileToOutputAndAssembly(j);

    printf("Success!\n");
    return 0;
}
