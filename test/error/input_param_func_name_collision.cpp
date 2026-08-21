#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    // Func declared before a scalar Param of the same name. Lowering
    // should produce a clean user_error rather than crashing.
    Var x;
    Func existing("foo");
    existing(x) = x;

    Param<int> p("foo");

    Func out("out");
    out(x) = existing(x) + p;

    out.compile_jit();

    printf("Should not get here\n");
    return 0;
}
