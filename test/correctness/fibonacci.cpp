#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {

    Func fib, g;
    Var x;
    RDom r(2, 18);

    fib(x) = 1;
    fib(r) = fib(r - 2) + fib(r - 1);

    g(x) = fib(x + 10);

    fib.compute_root();
    Buffer<int> out = g.realize({10});

    int fib_ref[20];
    fib_ref[0] = fib_ref[1] = 1;
    for (int i = 2; i < 20; i++) {
        fib_ref[i] = fib_ref[i - 1] + fib_ref[i - 2];
        if (i >= 10) {
            if (fib_ref[i] != out(i - 10)) {
                printf("out(%d) = %d instead of %d\n", i - 10, out(i - 10), fib_ref[i]);
                return 1;
            }
        }
    }

    printf("Success!\n");

    return 0;
}
