#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Func f;
    Var x;
    f.define_extern("test", {}, Int(32), {x});
    Var xo;
    f.split(x, xo, x, 8).serial(x);

    f.compile_jit();
    return 0;
}
