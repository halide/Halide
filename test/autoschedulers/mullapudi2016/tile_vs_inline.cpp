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

    int W = 1024;
    int H = 1024;

    Buffer<uint16_t> input(W, H, 3);

    for (int c = 0; c < 3; c++) {
        for (int y = 0; y < input.height(); y++) {
            for (int x = 0; x < input.width(); x++) {
                input(x, y, c) = rand() & 0xfff;
            }
        }
    }

    Var x("x"), y("y"), z("z"), c("c");
    Func f("f"), g("g");
    f(x, y, z, c) = (input(x, y, c) - input(x, z, c));
    g(x, y, c) = f(x, y, (x + y) % 10, c) +
                 f(x, y + 1, (x * y) % 10, c) +
                 f(x, y + 2, (x - y) % 10, c) +
                 f(x + 1, y, (x) % 10, c) +
                 f(x + 2, y, (y) % 10, c);

    // Provide estimates on the pipeline output
    g.set_estimates({{0, input.width() - 2}, {0, input.height() - 2}, {0, 3}});

    // Auto-schedule the pipeline
    Target target = get_jit_target_from_environment();
    Pipeline p(g);

    p.apply_autoscheduler(target, {"Mullapudi2016"});

    // Inspect the schedule (only for debugging))
    // g.print_loop_nest();

    // Run the schedule
    Buffer<uint16_t> out = p.realize({input.width() - 2, input.height() - 2, 3});

    printf("Success!\n");
    return 0;
}
