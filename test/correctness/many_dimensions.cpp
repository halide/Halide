#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {

    // Funcs inside a pipeline can have lots of dimensions.
    std::vector<Var> vars(20);
    Func f;
    Expr e = 0;
    for (size_t i = 0; i < vars.size(); i++) {
        vars[i] = Var();
        e += vars[i];
    }

    // f is a 20-dimensional function that evaluates to the sum of the args.
    f(vars) = e;

    Func g;
    Var x, y;

    // Compute two 20-dimensional sites at which to compute f, with
    // 0-1 indices. The site depends on x and y. In general this will
    // define two corners of a hypercube to be evaluated at each pixel
    // of the output.
    std::vector<Expr> site1(vars.size()), site2(vars.size());
    for (size_t i = 0; i < vars.size(); i++) {
        site1[i] = rand() & 0x1;
        if (rand() & 1) site1[i] += x;
        if (rand() & 1) site1[i] += y;
        site1[i] = site1[i] % 2;

        site2[i] = rand() & 0x1;
        if (rand() & 1) site2[i] += x;
        if (rand() & 1) site2[i] += y;
        site2[i] = site2[i] % 2;

        // To stop the hypercube realized from getting
        // too-high-dimensional, we make many of the coords match so
        // that it has extent one in many of its dimensions.
        if (rand() & 4) {
            site1[i] = site2[i];
        }
    }
    g(x, y) = f(site1) + f(site2);

    f.compute_at(g, x);
    g.realize(10, 10);

    printf("Success!\n");

    return 0;
}
