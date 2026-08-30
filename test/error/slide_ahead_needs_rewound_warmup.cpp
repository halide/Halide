#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Var x, y;
    Func f, g;

    f(x, y) = x + y;

    // The consumer walks r twice as fast as it walks f, so f gains a line
    // only on every other iteration. A window that advances unevenly is
    // warmed up with a select rather than by rewinding the loop, and a
    // producer running ahead of its consumer needs the rewound kind.
    RDom r(0, 16, "r");
    g(x, y) = 0;
    g(x, y) += f(x, r / 2) + y;

    f.store_at(g, x).compute_at(g, r).slide(g, r, 1);

    Buffer<int> im = g.realize({8, 8});

    printf("Success!\n");
    return 0;
}
