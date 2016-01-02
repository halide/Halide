#include <stdio.h>
#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Var x;

    Func one_arg;
    one_arg(x) = x * 2;   // One argument

    Func bad_call;
    bad_call() = one_arg; // Treated as Expr, results in implicit arg, but RHS doesn't have a _.

    // Should result in an error
    Image<uint32_t> result = bad_call.realize(256, 256);

    printf("Success!\n");
    return 0;
}
