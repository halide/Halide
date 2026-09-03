#include "Halide.h"
#include <stdio.h>

using namespace Halide;

// A Func that reads its previous step may not be folded to a single slot:
// the point's own store would overwrite the step it reads wherever the
// schedule computes a point twice, and nothing checks that it doesn't.
int main(int argc, char **argv) {
    Func f(Int(32), 1, "f"), g("g");
    Var t("t"), to("to"), ti("ti");
    f(t) = select(t <= 0, 1, likely(f(t - 1) * 3 % 1000 + 1));
    g(t) = f(t) + 0;

    g.split(t, to, ti, 4, TailStrategy::RoundUp).unroll(ti);
    f.store_root().compute_at(g, ti).slide(g, t).fold_storage(t, 1);

    g.realize({64});

    printf("Success!\n");
    return 0;
}
