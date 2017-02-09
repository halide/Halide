#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Func f("f"), g("g");
    Var x, xi;

    g(x) = x;
    f(x) = g(x-2) + g(x+2);

    g.align_storage(x, 8).fold_storage(x, 8).store_root().compute_at(f, xi);
    // Uncommenting this would be great, because all access to g would
    // be at constant indices, and the values could live in 8
    // registers, but sliding window can't handle it. It only slides
    // over x or xi, not their combination.
    
    f.bound(x, 0, 1024).split(x, x, xi, 8, TailStrategy::RoundUp);
    
    f.realize(100);
    
    return 0;
}
