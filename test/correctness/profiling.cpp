#include "Halide.h"
using namespace Halide;

#include <stdio.h>

int main(int argc, char **argv) {
    Func f;
    Var x;
    f(x) = x;

    f.compile_jit(parse_target_string("host"));
    f.realize(100);

    Func g;
    g(x) = x;
    g.compile_jit(parse_target_string("host-profile"));
    g.realize(100);

    return 0;
}
