#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Func f(std::vector<Type>{Int(32), Int(32)}, "f"), g("g");

    Var x("x");

    // We can't fold the storage to size 1 because Halide could overwrite the first Tuple element before reading it
    // to compute the second Tuple element.
    f(x) = select(x <= 0, Tuple(0, 0), Tuple(f(x - 1)[0] + 1, f(x - 1)[1] + f(x - 1)[0]));
    g(x) = f(x)[1] * 2;

    f.compute_at(g, x).store_root().fold_storage(x, 1);
    g.realize({10});

    printf("Success!\n");
    return 0;
}
