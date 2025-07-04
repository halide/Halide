#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {

    Var x{"x"};

    {
        // Compute fibonacci:
        Func f1;
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
    }

    {
        // Now use undef in a tuple. The following code ping-pongs between the two tuple components using a stencil:
        RDom rx(0, 100);
        Func f3;
        f3(x) = Tuple(undef<float>(), sin(x));
        Expr left = max(rx - 1, 0);
        Expr right = min(rx + 1, 99);

        for (int i = 0; i < 10; i++) {
            f3(rx) = Tuple((f3(rx)[1] + f3(left)[1] + f3(right)[1]) / 3, undef<float>());
            f3(rx) = Tuple(undef<float>(), (f3(rx)[0] + f3(left)[0] + f3(right)[0]) / 3);
        }

        Buffer<float> o1(100), o2(100);
        o1.fill(17);
        o2.fill(18);
        f3.realize({o1, o2});

        for (int i = 0; i < 100; i++) {
            if (std::abs(o1(i)) > 1 || std::abs(o2(i)) > 1) {
                printf("Output outside of [-1, 1]: o1(%d) = %f, o2(%d) = %f\n", i, o1(i), i, o2(i));
                return 1;
            }
        }
    }

    {
        // From https://github.com/halide/Halide/issues/8667
        Var x{"x"};
        Func f{"f"}, g{"g"};

        // f is undef away from zero
        f(x) = select(x == 0, x + 1, undef<int>());
        // g is undef outside of [0, 1]
        g(x) = select(x == 0, f(x), -f(1 - x));

        Buffer<int> output(4);
        output.fill(17);
        g.realize(output);
        int expected[4] = {1, -1, 17, 17};
        for (int i = 0; i < 4; i++) {
            if (expected[i] != output(i)) {
                printf("Mismatch at index %d: expected %d, got %d\n", i, expected[i], output(i));
                return 1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
