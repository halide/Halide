#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Func f("f");
    Var x("x"), y("y");
    RDom r1(0, 10, "r1"), r2(0, 10, "r2"), r3(0, 10, "r3");

    f(x, y) = product(sum(r1, r1 + r3) + sum(r2, r2 * 2 + r3));

    // Is this the product over r1, or r3? It must be r3 because r1 is
    // used on the LHS, but Halide's not smart enough to know
    // that. All it sees is a product over an expression with two
    // reduction domains.
    f(r1, y) += product(sum(r2, r1 + r2 + r3));

    Buffer<int> result = f.realize(10, 10);

    return 0;
}
