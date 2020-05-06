
#include "Halide.h"
#include "halide_benchmark.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Var x, y;
    Func f, g;
    Param<bool> p;
    f(x, y) = x + y;
    g(x, y) = x + y;

    Func output;
    output(x, y) = select(x > 10, f(x, y), g(x, y));

    f.compute_at(output, y);
    g.compute_at(output, y);
    output.specialize(output.output_buffer().dim(0).min() == 0 &&
                      output.output_buffer().dim(0).extent() == 5);

    // The region required of f in the specialization is zero (or
    // actually negative-sized, given how we compute things), but
    // we don't find that out until bounds inference runs, so it
    // still gets a Realize node.  Compile this to make sure
    // allocation bounds inference doesn't get confused trying to
    // come up with a bound for it.

    output.compile_jit();

    printf("Success!\n");

    return 0;
}
