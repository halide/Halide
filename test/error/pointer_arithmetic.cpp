#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Param<const char *> p;
    p.set("Hello, world!\n");

    Func f;
    Var x;
    // Should error out during match_types
    f(x) = p + 2;

    return 0;
}
