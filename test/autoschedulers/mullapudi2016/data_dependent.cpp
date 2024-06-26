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

    int W = 800;
    int H = 800;
    Buffer<uint16_t> input(W, H);

    for (int y = 0; y < input.height(); y++) {
        for (int x = 0; x < input.width(); x++) {
            input(x, y) = rand() & 0xfff;
        }
    }

    Var x("x"), y("y"), c("c");

    Func f("f");
    f(x, y, c) = print_when(x < 0, input(x, y) * input(c, c));

    Func g("g");
    g(x, y) = (f(x, y, input(x, y) % 10) + f(x + 1, y, (input(x, y) - 1) % 10)) / 2;

    // Provide estimates on the pipeline output
    g.set_estimate(x, 0, input.width() - 1).set_estimate(y, 0, input.height());

    // Auto-schedule the pipeline
    Target target = get_jit_target_from_environment();
    Pipeline p(g);

    p.apply_autoscheduler(target, {"Mullapudi2016"});

    // Inspect the schedule (only for debugging))
    // g.print_loop_nest();

    // Run the schedule
    Buffer<uint16_t> out = p.realize({input.width() - 1, input.height()});

    printf("Success!\n");
    return 0;
}
