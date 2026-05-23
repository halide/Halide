#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int counter = 0;
extern "C" HALIDE_EXPORT_SYMBOL int call_count(int x) {
    counter++;
    assert(counter > 0);
    return 99;
}
HalideExtern_1(int, call_count, int);

void check(const Buffer<int> &im, const Buffer<int> &correct) {
    for (int y = 0; y < im.height(); y++) {
        for (int x = 0; x < im.width(); x++) {
            if (im(x, y) != correct(x, y)) {
                printf("Value at %d %d was %d instead of %d\n",
                       x, y, im(x, y), correct(x, y));
                exit(1);
            }
        }
    }
}

int main(int argc, char **argv) {

    Var x, y;

    Buffer<int> ref;
    for (int i = 0; i < 2; i++) {
        // Could slide this reduction over y, but we don't, because it's
        // too hard to implement bounds analysis on the intermediate
        // stages.
        Func f("f");
        f(x, y) = x;
        f(0, y) += f(1, y) + f(0, y);
        f(x, y) += call_count(f(x, y));

        Func g("g");
        g(x, y) = f(x, y) + f(x, y - 1) + f(x, y - 2);

        if (i == 0) {
            f.compute_root();
            ref = g.realize({2, 10});
        } else {
            f.store_root().compute_at(g, y);
            counter = 0;
            check(g.realize({2, 10}), ref);

            int correct = 24;
            if (counter != correct) {
                printf("Failed sliding a reduction: %d evaluations instead of %d\n", counter, correct);
                return 1;
            }
        }
    }

    for (int i = 0; i < 2; i++) {
        // Can't slide this reduction over y, because the second stage scatters.
        Func f("f");
        f(x, y) = x;
        f(x, x) += f(x, 0) + f(x, 1);
        f(x, y) += call_count(f(x, y));

        Func g("g");
        g(x, y) = f(x, y) + f(x, y - 1) + f(x, y - 2);

        if (i == 0) {
            f.compute_root();
            ref = g.realize({2, 10});
        } else {
            f.store_root().compute_at(g, y);

            counter = 0;
            check(g.realize({2, 10}), ref);

            int correct = 60;
            if (counter != correct) {
                printf("Failed sliding a reduction: %d evaluations instead of %d\n", counter, correct);
                return 1;
            }
        }
    }

    for (int i = 0; i < 2; i++) {
        // Would be able to slide this so that we only have to compute
        // one new row of f per row of g, but the unroll in the first
        // stage forces evaluations of size two in y, which would
        // clobber earlier values of the final stage of f, so we have
        // to compute the final stage of f two rows at a time as well.

        // The result is that we extend the loop to warm up f by 2
        // iterations. This adds up to 2*(12*2) = 48 evaluations of f.
        Func f("f");
        f(x, y) = x;
        f(0, y) += f(1, y) + f(2, y);
        f(x, y) += call_count(f(x, y));

        f.unroll(y, 2, TailStrategy::GuardWithIf);
        f.update(0).unscheduled();
        f.update(1).unscheduled();

        Func g("g");
        g(x, y) = f(x, y) + f(x, y - 1) + f(x, y - 2);

        if (i == 0) {
            f.compute_root();
            ref = g.realize({2, 10});
        } else {
            f.store_root().compute_at(g, y);

#ifdef HALIDE_USE_LOOP_REWINDING_EVEN_THOUGH_IT_IS_BROKEN_SEE_ISSUE_8140
            counter = 0;
            check(g.realize({2, 10}), ref);
            int correct = 48;

            if (counter != correct) {
                printf("Failed sliding a reduction: %d evaluations instead of %d\n", counter, correct);
                return 1;
            }
#else
            // This version is unfortunately busted, because the different
            // stages of f somehow get different bounds for the y dimension.

            // The evaluation order for the first iteration (y == 0) a region of
            // size 3 is required of f, so the rows computed are:

            // f stage 0 rows -2 -1, -1 0 (-1 is repeated due to the ShiftInwards unroll)
            // f stage 1 rows -2 -1 0
            // f stage 2 rows -2 -1 0
            // g stage 0 row 0 (which uses f rows -2 -1 0)

            // For the next row, which is the steady-state, we have:
            // f stage 0 rows 0 1
            // f stage 1 row 1 (row 0 is missing!)
            // f stage 2 rows 0 1
            // g stage 0 row 1 (which uses f rows -1 0 1)

            // I believe this is a variant of issue #7819, which describes how
            // overcompute of sliding window stages is problematic.
#endif
        }
    }

    printf("Success!\n");
    return 0;
}
