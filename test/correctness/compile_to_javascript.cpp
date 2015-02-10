#include <Halide.h>
#include <stdio.h>
#ifndef _MSC_VER
#include <unistd.h>
#endif

using namespace Halide;

void compile_javascript(Func j) {
    const char *fn_object = "javascript.js";

    #ifndef _MSC_VER
    if (access(fn_object, F_OK) == 0) { unlink(fn_object); }
    assert(access(fn_object, F_OK) != 0 && "Output file already exists.");
    #endif

    std::vector<Argument> empty_args;
    j.compile_to_javascript(fn_object, empty_args, "");

    #ifndef _MSC_VER
    assert(access(fn_object, F_OK) == 0 && "Output file not created.");
    #endif
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

    compile_javascript(j);

    printf("Success!\n");
    return 0;
}
