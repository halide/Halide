#include "Halide.h"
#include <stdio.h>

using namespace Halide;

void run_test_1() {
    ImageParam input(UInt(8), 2);
    Var x("x"), y("y");

    RDom r(0, 2, "r");

    // Make a sum: a non-pure function with
    // update definitions.
    Func f("f");
    f(x, y) = sum( input(x + r.x, y) );

    // Consume the sum in a way that the
    // autoscheduler will not know its bounds:
    // by depending on a library function call.
    // The autoscheduler should not attempt
    // to inline "sum" as a result.
    Func g("g");
    g(x, y) = f( cast<int>(Halide::sin(x)) + x, y);

    // Provide estimates on the pipeline output
    g.estimate(x, 0, 1000).estimate(y, 0, 1000);

    // Provide estimates on the ImageParam
    input.dim(0).set_bounds_estimate(0, 1000);
    input.dim(1).set_bounds_estimate(0, 1000);

    // Auto-schedule the pipeline
    Target target = get_jit_target_from_environment();
    Pipeline p(g);

    p.auto_schedule(target);

    // Inspect the schedule
    g.print_loop_nest();
}

int main(int argc, char **argv) {
    std::cout << "Test 1:" << std::endl;
    run_test_1();
    printf("Success!\n");
    return 0;
}
