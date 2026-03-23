#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Func f;
    Var x;

    // Compute the GCD of two numbers using the euclidean algorithm.
    Param<int> pa, pb;

    // Sort the inputs. We'll maintain the invariant that a >= b.
    Expr a = max(pa, pb), b = min(pa, pb);

    f() = {a, b, 0};

    // The worst-case number of iterations occurs when the smaller
    // number is 1. Iterating up to 'a' should suffice.
    RDom r(0, a);
    a = f()[0];
    b = f()[1];

    // Stop looping when b hits zero. It would be nice if this created
    // an early-exit from the reduction loop, but that doesn't
    // currently happen.
    r.where(b != 0);
    f() = {b, a % b, r};

    // Let's unroll it. This originally triggered two bugs:

    // 1) There are let statements that get unified, even though they
    // include stores with values that change.

    // 2) There are if statements with the same condition that get
    // unified, even though the value of the condition depends on a
    // load whose value may have changed within the body of the first
    // if.
    f.update().unroll(r, 4);

    pa.set(131 * 151 * 2);
    pb.set(131 * 157 * 3);

    int result = evaluate<int>(f()[0]);
    int correct = 131;
    if (result != correct) {
        printf("Bad GCD: %d != %d\n", result, correct);
        return 1;
    }

    printf("Success!\n");
    return 0;
}
