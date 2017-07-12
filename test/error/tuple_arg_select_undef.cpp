#include "Halide.h"
#include <stdio.h>

using namespace Halide;
using namespace Halide::Internal;

int main(int argc, char **argv) {
    Var x("x"), y("y");
    Func f("f"), g("g");

    f(x, y) = {0, 0};

    RDom r(0, 10);
    Expr arg_0 = clamp(select(r.x < 2, 13, undef<int>()), 0, 20);
    Expr arg_1 = clamp(select(r.x < 5, 23, undef<int>()), 0, 20);
    // Different predicates for the undefs: should result in an error
    f(arg_0, arg_1) = {f(arg_0, arg_1)[0] + 10, f(arg_0, arg_1)[1] + 5};

    f.realize(100, 100);

    printf("Success!\n");
    return 0;
}
