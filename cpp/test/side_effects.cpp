#include <stdio.h>
#include <Halide.h>
#include <math.h>
#include <dlfcn.h>

using namespace Halide;

// NB: You must compile with -rdynamic for llvm to be able to find the appropriate symbols
// This is not supported by the C PseudoJIT backend.

// Many things that are difficult to do with Halide can be hacked in
// using reductions that call extern C functions. One example is
// argmin

extern "C" int argmin(int x, float y) {
    static float minVal = 1e10;
    static int minIndex = 0;
    if (y < minVal) {
        minIndex = x;
        minVal = y;
    }
    return minIndex;
}
HalideExtern_2(int, argmin, int, float);

int main(int argc, char **argv) {
    Var x, y;
    Func f, g, h;

    printf("Defining function...\n");

    f(x) = sin(x/10.0f+17);

    // Compute argmin of f over [-100..100]
    RDom r(-100, 100);
    g(x) = 0;
    g(x) = argmin(r, f(r));

    Image<int> result = g.realize(1);
    int idx = result(0);
    printf("sin(%d/10.0f+17) = %f\n", idx, sinf(idx/10.0f+17));

    printf("Success!\n");
    return 0;
}
