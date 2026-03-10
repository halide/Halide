#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Func f("f"), g("g");
    Var x("x");

    f(x) = x;
    g(x) = x + 1;

    // g is an output, so it can't be scheduled compute_at another Func, even if
    // that func is realized first and is also an output.
    g.compute_at(f, x);

    Pipeline({f, g}).compile_jit();

    printf("Success!\n");
    return 0;
}
