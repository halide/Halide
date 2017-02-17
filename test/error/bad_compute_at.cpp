#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Func f("f"), g("g"), h("h"), junk1, junk2, junk3;
    Var x("x"), y("y");

    f(x) = x;
    g(x) = f(x);
    junk1(x) = 3;
    junk2(x) = 3;
    junk3(x, y) = 3;
    h(x, y) = g(x) + f(x) + junk1(x) + junk2(x) + junk3(x, y);

    g.compute_at(h, y);

    // Add some other junk functions to complicate the error message
    junk1.compute_at(h, y);
    junk2.compute_at(h, x);
    junk3.compute_root();

    // This makes no sense, because f is also used by g, which is computed at (h, y), which is outside of (h, x).
    f.compute_at(h, x);

    h.realize(10);

    printf("I should not have reached here\n");
    return 0;
}
