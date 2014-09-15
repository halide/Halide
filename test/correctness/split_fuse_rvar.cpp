#include <stdio.h>
#include <Halide.h>
#include <iostream>

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
        g(r.x, r.y) = f(r.y*4 + r.x);

        RVar rxy, rxyo, rxyi;
        g.update(0).fuse(r.x, r.y, rxy).split(rxy, rxyo, rxyi, 2);

        Image<int> Rg = g.realize(4, 4);

        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 4; j++) {
                if (Rg(j, i) != i*4 + j) {
                    std::cout << "Error! Expected " << i*4 + j << " at g(" << j << ", " << i << "), got " << Rg(j, i) << std::endl;
                    return -1;
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

        Image<int> Rg = g.realize(16);

        for (int i = 0; i < 16; i++) {
            if (Rg(i) != i) {
                std::cout << "Error! Expected " << i << " at g(" << i << "), got " << Rg(i) << std::endl;
                return -1;
            }
        }
    }

    printf("Success!");
    return 0;
}
