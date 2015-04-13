#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    // Turning on tracing wraps certain Exprs. This shouldn't effect
    // bounds inference.

    Func f, g;
    Var x;
    f(x) = clamp(x, 0, 100);
    f.compute_root();
    g(x) = f(f(x));
    // f is known to be bounded, so this means we need 101 values of
    // f. This shouldn't be confused by tracing loads of f or stores
    // to g.
    f.trace_loads();
    g.trace_stores();

    // Shouldn't throw an error about unbounded access.
    g.compile_jit();

    printf("Success!\n");

    return 0;
}
