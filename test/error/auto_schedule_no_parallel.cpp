#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Func fib, g;
    Var x;
    RDom r(2, 18);

    fib(x) = 1;
    fib(r) = fib(r - 2) + fib(r - 1);

    g(x) = fib(x + 10);

    // Provide estimates for pipeline output
    g.set_estimate(x, 0, 50);

    // Partially specify some schedules
    g.parallel(x);

    // Auto schedule the pipeline
    Target target = get_target_from_environment();
    Pipeline p(g);

    // This should throw an error since auto-scheduler does not currently
    // support partial schedules
    p.apply_autoscheduler(target, {"Mullapudi2016"});

    printf("Success!\n");
    return 0;
}
