#include "Halide.h"
#include <stdio.h>

// A simple reduction (no rfactor) with a single aligned split, tried with
// GuardWithIf, RoundUpAndBlend, and ShiftInwardsAndBlend.
//
// The split here is of the pure var x, not of the RDom's r: Stage::split
// only allows GuardWithIf or Predicate when splitting an RVar itself (see
// Func.cpp), since RoundUp/ShiftInwards-family strategies would change the
// meaning of the reduction by recomputing or overrunning it. Splitting a
// pure var of an update definition doesn't have that restriction, and
// RoundUpAndBlend/ShiftInwardsAndBlend are exactly the tail strategies
// meant for vectorizing an update like this one (see their doc comments in
// Schedule.h).
//
// This is the same boundary-handling code in ApplySplit.cpp's
// ShiftInwardsAndBlend/RoundUpAndBlend branches exercised by
// rfactor_split_aligned_nested.cpp, but without rfactor's extra layer of
// indirection (splitting a var that's already itself the result of an
// aligned split) -- here x's own bounds are simple compile-time constants,
// so this isolates the aligned-split-plus-blend mechanics on their own.

using namespace Halide;

int main(int argc, char **argv) {
    for (auto ts : {TailStrategy::GuardWithIf, TailStrategy::RoundUpAndBlend, TailStrategy::ShiftInwardsAndBlend}) {
        printf("Testing tail strategy: %d\n", (int)ts);

        Var x{"x"}, xo{"xo"}, xi{"xi"};
        Func h{"h"};
        RDom r(0, 5, "r");
        Param<int> p{"p"};
        p.set_range(0, 3);

        h(x) = 0;
        h(x) += x + r;
        h.compute_root();

        h.update(0)
            .split(x, xo, xi, 4, p, ts)
            .vectorize(xi);

        // h is read through a further Func rather than realized directly,
        // so that RoundUpAndBlend/ShiftInwardsAndBlend get an
        // internally-allocated (and thus paddable) buffer to blend into,
        // instead of a caller-provided one of a fixed, non-factor-multiple
        // size.
        Func out{"out"};
        out(x) = h(x);

        for (int a = 0; a < 4; a++) {
            p.set(a);
            Buffer<int> im = out.realize({37});
            for (int x = 0; x < 37; x++) {
                int expected = 5 * x + 10;
                if (im(x) != expected) {
                    printf("im(%d) = %d instead of %d (p: %d, ts: %d)\n", x, im(x), expected, a, (int)ts);
                    return 1;
                }
            }
        }
    }

    printf("Success!\n");
    return 0;
}
