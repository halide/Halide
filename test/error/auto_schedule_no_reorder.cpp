#include <stdio.h>
#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Func f, g;
    Var x, y;
    RDom r(2, 18);

    f(x, y) = 1;
    f(r, y) = f(r-2, y) + f(r-1, y);

    g(x, y) = f(x+10, y) + 2;

    // Provide estimates for pipeline output
    g.estimate(x, 0, 50);
    g.estimate(y, 0, 50);

    // Partially specify some schedules
    g.reorder(y, x);

    // Auto schedule the pipeline
    Target target = get_target_from_environment();
    Pipeline p(g);

    // This should throw an error since auto-scheduler does not currently
    // support partial schedules
    p.auto_schedule(target);

    printf("Success!\n");
    return 0;
}
