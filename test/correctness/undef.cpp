#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {

    // Compute fibonacci:
    Func f1;
    Var x;
    RDom r(2, 100);

    // Pure definition
    f1(x) = 0;
    // Update rule
    f1(r) = f1(r - 1) + f1(r - 2);

    Buffer<int> fib1 = f1.realize({102});

    // That code needlessly set the entire buffer to zero before
    // computing fibonacci. We know for our use of fibonacci that
    // we'll never ask for values that haven't been set by the update
    // step, except for entires 0 and 1. But Halide can't prove this,
    // because a user may realize fib over a negative region, or
    // beyond 102.

    // Now we'll compute fibonacci without initializing all the
    // entries first. This promises that we don't care about values
    // outside of the range written by the update steps, and that all
    // values recursively read by an update step have been previously
    // written by an earlier update step.
    Func f2;

    // This line just serves to name the pure variable (x) and define
    // the type of the function (int).
    f2(x) = undef<int>();

    // This actually turns into code:
    f2(0) = 0;
    f2(1) = 0;
    f2(r) = f2(r - 1) + f2(r - 2);

    Buffer<int> fib2 = f2.realize({102});

    int err = evaluate_may_gpu<int>(maximum(fib1(r) - fib2(r)));
    if (err > 0) {
        printf("Failed\n");
        return 1;
    }

    // Now use undef in a tuple. The following code ping-pongs between the two tuple components using a stencil:
    RDom rx(0, 100);
    Func f3;
    f3(x) = Tuple(undef<float>(), sin(x));
    Expr left = max(rx - 1, 0);
    Expr right = min(rx + 1, 99);

    for (int i = 0; i < 10; i++) {
        f3(rx) = Tuple(f3(rx)[0] + f3(rx)[1] + f3(left)[1] + f3(right)[1], undef<float>());
        f3(rx) = Tuple(undef<float>(), f3(rx)[1] + f3(rx)[0] + f3(left)[0] + f3(right)[0]);
    }
    f3.realize({100});

    printf("Success!\n");
    return 0;
}
