#include <stdio.h>
#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {

    Func fib, g;
    Var x;
    RDom r(2, 18);

    fib(x) = 1;
    fib(r) = fib(r-2) + fib(r-1);

    g(x) = fib(x+10);

    // Provide estimates for pipeline output
    g.estimate(x, 0, 50);

    // Auto schedule the pipeline
    Target target = get_target_from_environment();
    Pipeline p(g);

    p.auto_schedule(target);

    // Inspect the schedule
    g.print_loop_nest();

    // Run the schedule
    Image<int> out = p.realize(10);

    printf("Success!\n");
    return 0;
}
