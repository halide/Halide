#include "Halide.h"

using namespace Halide;

// An update definition that is inductive in t (it reads the step before)
// and reduces over an RDom in x may vectorize and parallelize x without
// allow_race_conditions(): t's loop is serial by construction, so the
// race analysis treats it as shared between iterations of x, and the
// reads of step t-1 are of a finished step.
int main(int argc, char **argv) {
    const int W = 64, T = 8;
    Func f("f");
    Var x("x"), t("t");
    RDom r(0, W, "r");
    f(x, t) = 0.f;
    f(r, t) = select(t <= 0, cast<float>(r),
                     likely(f(r, t - 1) + f((r + 1) % W, t - 1) + 1.0f));

    RVar ro("ro"), ri("ri");
    f.update(0).split(r, ro, ri, 16).vectorize(ri).parallel(ro);

    // An inductive Func cannot be the output; a consumer reads it.
    Func g("g");
    g(x, t) = f(x, t);
    f.compute_root();
    Buffer<float> out = g.realize({W, T});

    std::vector<float> ref(W), next(W);
    for (int i = 0; i < W; i++) {
        ref[i] = (float)i;
    }
    for (int tt = 0; tt < T; tt++) {
        if (tt > 0) {
            for (int i = 0; i < W; i++) {
                next[i] = ref[i] + ref[(i + 1) % W] + 1.0f;
            }
            ref = next;
        }
        for (int i = 0; i < W; i++) {
            if (out(i, tt) != ref[i]) {
                printf("f(%d, %d) = %f instead of %f\n", i, tt, out(i, tt), ref[i]);
                return 1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
