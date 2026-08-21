#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    // Func declared before an ImageParam of the same name. Lowering
    // should produce a clean user_error rather than crashing.
    Var x;
    Func existing("foo");
    existing(x) = x;

    ImageParam ip(Int(32), 1, "foo");

    Func out("out");
    out(x) = existing(x) + ip(x);

    out.compile_jit();

    printf("Should not get here\n");
    return 0;
}
