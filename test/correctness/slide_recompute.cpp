#include "Halide.h"
#include <stdio.h>

using namespace Halide;

// A self-referential Func slid over a consumer loop that computes each
// step's points TWICE: a split of an extent-1 loop by 2 leaves both
// pieces covering the whole range. Recomputing a point is legal, but a
// one-slot window (the "dies at its store" shrink) would make the second
// computation read its own fresh output as the previous step and advance
// the recurrence twice. The window must keep the previous value.
int main(int argc, char **argv) {
    Func f(std::vector<Type>{Int(32), Int(32)}, 2, "f"), g("g");
    Var l("l"), t("t"), co("co"), ci("ci"), coo("coi"), coi("coi2"), to("to"), ti("ti");

    // Two coupled chains per lane; both components read both previous ones.
    f(l, t) = select(t <= 0, Tuple(l, 2 * l),
                     Tuple(likely(f(l, t - 1)[0] + f(l, t - 1)[1]), f(l, t - 1)[0] - l));
    g(l, t) = f(l, t)[0] + f(l, t)[1];

    g.split(l, co, ci, 16).reorder(ci, co, t).vectorize(ci);
    // co has extent 1 for a 16-wide output, so splitting it by 2 makes two
    // identical unrolled instances of f's compute per t iteration.
    g.split(co, coo, coi, 2).reorder(ci, coi, t, coo).unroll(coi);
    f.store_at(g, coo).compute_at(g, coi).vectorize(l, 16);
    f.slide(g, t).fold_storage(t, 2);
    g.split(t, to, ti, 2, TailStrategy::RoundUp).unroll(ti);

    Buffer<int> out = g.realize({16, 40});
    for (int l = 0; l < 16; l++) {
        int a = l, b = 2 * l;
        for (int t = 0; t < 40; t++) {
            if (t > 0) {
                int na = a + b, nb = a - l;
                a = na, b = nb;
            }
            if (out(l, t) != a + b) {
                printf("out(%d, %d) = %d instead of %d\n", l, t, out(l, t), a + b);
                return 1;
            }
        }
    }
    printf("Success!\n");
    return 0;
}
