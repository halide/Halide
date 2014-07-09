#include <stdio.h>
#include <Halide.h>

using namespace Halide;

int main(int argc, char **argv) {
    Func f, g, h;
    Var x;

    // Create a simple function computed at root.
    f(x) = x;
    f.compute_root();

    // Create a function that uses an undefined buffer after f is
    // freed.
    g(x) = undef<int>() + f(x);
    g(x) += 1;
    g.compute_root();

    // This just forces g to be stored somewhere (the re-used buffer).
    h(x) = g(x);
    h.compute_root();

    // Bound it so the allocations go on the stack.
    h.bound(x, 0, 16);

    Image<int> result = h.realize(16);
    for (int i = 0; i < result.width(); i++) {
        if (result(i) != i + 1) {
            printf("Error! Allocation did not get reused at %d (%d != %d)\n", i, result(i), i + 1);
            return -1;
        }
    }


    printf("Success!\n");
    return 0;
}
