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

    Var x("x"), y("y"), xi("xi"), yi("yi");
    Buffer<float> input = lambda(x, y, sin(x) + cos(y) + 1.0f).realize({2200, 2200});

    int num_levels = 10;

    std::vector<Func> down;
    for (int i = 0; i < num_levels; i++) {
        Func d("down_" + std::to_string(i));
        down.push_back(d);
    }

    std::vector<Func> up;
    for (int i = 0; i < num_levels; i++) {
        Func u("up_" + std::to_string(i));
        up.push_back(u);
    }

    down[0](x, y) = input(x, y);
    for (int i = 1; i < num_levels; i++) {
        down[i](x, y) = (down[i - 1](2 * x, y) + down[i - 1](2 * x + 1, y)) / 2;
    }

    up[0](x, y) = down[num_levels - 1](x, y);
    for (int i = 1; i < num_levels; i++) {
        up[i](x, y) = (up[i - 1](x / 2, y) + up[i - 1]((x + 1) / 2, y)) / 2;
    }

    // Provide esitmates for pipeline outputs
    up[num_levels - 1].set_estimates({{0, 1500}, {0, 1500}});

    // Auto-schedule the pipeline
    Target target = get_jit_target_from_environment();
    Pipeline p(up[num_levels - 1]);

    p.apply_autoscheduler(target, {"Mullapudi2016"});

    // Inspect the schedule (only for debugging))
    // up[num_levels - 1].print_loop_nest();

    // Run the schedule
    Buffer<float> out = p.realize({1500, 1500});

    printf("Success!\n");

    return 0;
}
