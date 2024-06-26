#include "Halide.h"
#include <algorithm>
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    {
        // Write a function that has a race condition not affecting the
        // output.
        Func f;
        Var x;
        RDom r(0, 100);

        f(x) = 0;
        f(r / 2) = r / 2;

        // If we parallelize over r, two threads store to each memory
        // location in parallel (r = 2*k and r = 2*k + 1 both store to
        // k). However this is fine, because they're both trying to
        // store the same value (k), so it doesn't matter who wins the
        // race.  Halide's not smart enough to understand this.
        f.update().allow_race_conditions().parallel(r);

        Buffer<int> out = f.realize({100});
        for (int i = 0; i < 100; i++) {
            int correct = (i < 50) ? i : 0;
            if (out(i) != correct) {
                printf("out(%d) = %d instead of %d\n",
                       i, out(i), correct);
                return 1;
            }
        }
    }

    {
        // Write a function that looks like it might have a race
        // condition, but doesn't.

        Func f;
        Var x;

        RDom r(0, 256);
        Expr permuted = (38 * r * r + 193 * r + 32) % 256;
        // There's actually a one-to-one mapping from r to permuted,
        // because permuted is a specially-constructed permutation
        // polynomial. We don't expect Halide to understand this
        // though, so it'll complain even though there's no race
        // condition. This is a case where it's safe to overrule
        // Halide's objection.

        f(x) = -1;
        f(permuted) = r;
        f.update().allow_race_conditions().vectorize(r, 4).parallel(r);

        Buffer<int> out = f.realize({256});

        // Sort the output.
        std::sort(&out(0), &out(256));

        // If we did indeed have a permutation, then the values should
        // now equal the indices.
        for (int i = 0; i < 256; i++) {
            if (out(i) != i) {
                printf("Error: after sorting, out(%d) was %d instead of %d\n", i, out(i), i);
                return 1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
