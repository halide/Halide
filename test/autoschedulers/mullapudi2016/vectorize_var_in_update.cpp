#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    if (get_jit_target_from_environment().arch == Target::WebAssembly) {
        printf("[SKIP] Autoschedulers do not support WebAssembly.\n");
        return 0;
    }

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <autoscheduler-lib>\n", argv[0]);
        return 1;
    }

    load_plugin(argv[1]);

    // This test is making sure that the auto-scheduler picks the appropriate
    // tail strategy when splitting the var of an update definition.
    // The default tail strategy for this case (i.e. RoundUp) will cause
    // an out-of-bound error if there are accesses to inputs or outputs.

    Buffer<int> input(50);
    for (int i = 0; i < 50; i++) {
        input(i) = i;
    }

    Func f("f"), g("g"), h("h"), in("in");
    Var x("x"), y("y");
    RDom r(0, 20, 0, 10);

    in(x, y) = x + y;
    in(x, y) += input(x) - input(y);

    f(x, y) = x * y;
    f(r.x, r.y) += in(r.x, r.y) + 3;
    f(x, y) += in(r.x, r.y) + 3;

    g(x, y) = x + y;
    g(x, y) += f(r.x, r.y) + 3;

    h(x, y) = x + y;
    h(x, y) += g(r.x, r.y) + 3;

    // Provide estimates on the pipeline output
    h.set_estimates({{0, 50}, {0, 50}});

    // Auto-schedule the pipeline
    Target target = get_jit_target_from_environment();
    Pipeline p(h);

    p.apply_autoscheduler(target, {"Mullapudi2016"});

    // Inspect the schedule (only for debugging))
    // h.print_loop_nest();

    // Run the schedule
    Buffer<int> out = p.realize({50, 50});

    printf("Success!\n");
    return 0;
}
