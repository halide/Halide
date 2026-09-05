#include "Halide.h"
#include <stdio.h>

using namespace Halide;

// The slid dimension is rebuilt from three loops, and the outer two both
// shift their last iteration inwards. The dimension steps backwards where
// each tail starts, so the window would be asked to move backwards.
int main(int argc, char **argv) {
    Func f(Int(32), 1, "f"), g("g");
    Var t("t"), to("to"), ti("ti"), too("too"), toi("toi");
    f(t) = select(t <= 0, 1, likely(f(t - 1) * 3 % 1000 + 1));
    g(t) = f(t) + 0;

    g.split(t, to, ti, 4, TailStrategy::ShiftInwards)
        .split(to, too, toi, 3, TailStrategy::ShiftInwards);
    f.store_root().compute_at(g, ti).slide(g, t);

    g.realize({30});

    printf("Success!\n");
    return 0;
}
