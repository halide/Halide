#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    // Loops deriving from the same original inductive variable must keep
    // their nesting order: swapping the pieces of a split makes the
    // composed traversal of the inductive var non-monotonic.

    Func f(Float(32), 1, "f"), g("g");
    Var t("t"), to("to"), ti("ti");
    f(t) = select(t <= 0, 0.f, likely(f(t - 1) + 1.f));
    g(t) = f(t);
    f.compute_root().split(t, to, ti, 4).reorder(to, ti);

    g.realize({16});

    printf("Success!\n");
    return 0;
}
