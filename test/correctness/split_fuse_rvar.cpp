#include "Halide.h"
#include <iostream>
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    {
        // Ensure we can re-split a fused RVar with a different factor.
        Var x("x"), y("y");
        Func f("f");
        f(x) = x;

        Func g;
        g(x, y) = undef<int>();
        RDom r(0, 4, 0, 4);
        g(r.x, r.y) = f(r.y * 4 + r.x);

        RVar rxy, rxyo, rxyi;
        g.update(0).fuse(r.x, r.y, rxy).split(rxy, rxyo, rxyi, 2);

        Buffer<int> Rg = g.realize({4, 4});

        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 4; j++) {
                int expected = i * 4 + j;
                if (Rg(j, i) != expected) {
                    printf("Error! Expected %d at g(%d, %d), got %d\n", expected, j, i, Rg(j, i));
                    return 1;
                }
            }
        }
    }

    {
        // Ensure we can re-fuse a split RVar with a different factor.
        Var x("x");
        Func f("f");
        f(x) = x;

        Func g;
        g(x) = undef<int>();
        RDom r(0, 16);
        g(r) = f(r);

        RVar ro, ri, roi;
        g.update(0).split(r, ro, ri, 2).fuse(ro, ri, roi);

        Buffer<int> Rg = g.realize({16});

        for (int i = 0; i < 16; i++) {
            if (Rg(i) != i) {
                printf("Error! Expected %d at g(%d), got %d\n", i, i, Rg(i));
                return 1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
