#include "Halide.h"

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

    Var x("x"), y("y");
    Func f("f"), g("g"), h("h");

    g(x) = x;
    g(x) += 10;
    h(x) = x * x;
    f(x) = select(false, g(x + 1), h(x + 1));

    f.set_estimates({{0, 256}});

    Target target = get_jit_target_from_environment();
    Pipeline p(f);

    p.apply_autoscheduler(target, {"Mullapudi2016"});

    // Inspect the schedule (only for debugging))
    // f.print_loop_nest();

    // Run the schedule
    p.realize({256});

    printf("Success!\n");
    return 0;
}
