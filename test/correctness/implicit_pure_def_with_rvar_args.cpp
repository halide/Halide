#include "Halide.h"
#include <cstdio>

using namespace Halide;

// Regression test for https://github.com/halide/Halide/issues/9102
//
// When a Func's first definition is an update that uses RVars directly
// as LHS args (e.g. h(r.x) += ...), Halide auto-generates an implicit
// pure definition. The pure dimension must not share a name with the
// RVar, or bounds inference resolves the update's RVar loop bounds to
// the pure dimension's (buffer-driven) bounds instead of the RDom's.

int main(int argc, char **argv) {
    Var x;

    // Case 1: the original reproducer. vectorize(r.x) requires the RVar
    // loop to have a constant extent; before the fix, bounds inference
    // produced a symbolic extent from the output buffer and this
    // schedule failed to compile.
    {
        RDom r(0, 16, 0, 8);
        Func f{"f"}, g{"g"}, h{"h"};
        f(x) = x + 1;
        g(x) = 2 * x + 3;

        h(r.x) += f(r.x + r.y) * g(r.y);

        f.compute_root();
        g.compute_root();
        h.update().atomic().vectorize(r.x).unroll(r.y);

        Buffer<int> out = h.realize({16});
        for (int i = 0; i < 16; i++) {
            int expected = 0;
            for (int j = 0; j < 8; j++) {
                expected += (i + j + 1) * (2 * j + 3);
            }
            if (out(i) != expected) {
                printf("Case 1: out(%d) = %d, expected %d\n", i, out(i), expected);
                return 1;
            }
        }
    }

    // Case 2: same computation, but with an explicit pure definition.
    // This was the user's workaround; it must still give the same answer.
    {
        RDom r(0, 16, 0, 8);
        Func f{"f2"}, g{"g2"}, h{"h2"};
        f(x) = x + 1;
        g(x) = 2 * x + 3;

        h(x) = 0;
        h(r.x) += f(r.x + r.y) * g(r.y);

        f.compute_root();
        g.compute_root();
        h.update().atomic().vectorize(r.x).unroll(r.y);

        Buffer<int> out = h.realize({16});
        for (int i = 0; i < 16; i++) {
            int expected = 0;
            for (int j = 0; j < 8; j++) {
                expected += (i + j + 1) * (2 * j + 3);
            }
            if (out(i) != expected) {
                printf("Case 2: out(%d) = %d, expected %d\n", i, out(i), expected);
                return 1;
            }
        }
    }

    // Case 3: RDom bounds narrower than the realized output. Exercises
    // the underlying bounds bug directly (no vectorize needed): without
    // a correct loop bound from the RDom, the update would write to
    // indices outside the RDom, producing wrong values at the ends.
    {
        RDom r(2, 5);
        Func h{"h3"};
        h(r) += cast<int>(r) * 10;

        h.update().vectorize(r, 4, TailStrategy::GuardWithIf);

        Buffer<int> out = h.realize({10});
        for (int i = 0; i < 10; i++) {
            int expected = (i >= 2 && i < 7) ? i * 10 : 0;
            if (out(i) != expected) {
                printf("Case 3: out(%d) = %d, expected %d\n", i, out(i), expected);
                return 1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
