// Minimal reproducer: an inductive Func with a TWO-deep lookback, slid
// (explicit slide directive) over a split consumer dimension, produces
// wrong values. The one-deep case is fine; the unsplit two-deep case is
// fine.
#include "Halide.h"
#include <cstdio>
using namespace Halide;

int main() {
    const int T = 64;
    for (int split : {0, 1}) {
        Var t("t");
        Func f(Float(32), "f");
        // A two-deep recurrence in the additive shape the classifier
        // accepts (the biquad cascade's shape).
        f(t) = 1.f + select(t < 1, 0.f, likely(0.5f * f(t - 1))) +
               select(t < 2, 0.f, likely(0.25f * f(t - 2)));
        Func g("g");
        g(t) = f(t);
        if (split) {
            Var to("to"), ti("ti");
            g.split(t, to, ti, 4, TailStrategy::RoundUp);
            f.store_root().compute_at(g, ti).slide(g, t).fold_storage(t, 8);
        } else {
            f.store_root().compute_at(g, t).slide(g, t).fold_storage(t, 8);
        }
        Buffer<float> out = g.realize({T});
        // Reference.
        float a = 0.f, b = 0.f;  // f(t-1), f(t-2)
        double err = 0;
        for (int i = 0; i < T; i++) {
            float v = 1.f + (i < 1 ? 0.f : 0.5f * a) + (i < 2 ? 0.f : 0.25f * b);
            err = std::max(err, (double)std::abs(out(i) - v));
            b = a;
            a = v;
        }
        if (err > 1e-6) {
            printf("split=%d wrong: max error %.3e\n", split, err);
            return 1;
        }
    }
    printf("Success!\n");
    return 0;
}
