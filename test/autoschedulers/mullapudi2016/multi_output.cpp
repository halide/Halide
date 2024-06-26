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

    int W = 1000;
    int H = 1000;
    Buffer<uint16_t> input(W, H);

    for (int y = 0; y < input.height(); y++) {
        for (int x = 0; x < input.width(); x++) {
            input(x, y) = rand() & 0xfff;
        }
    }

    Var x("x"), y("y");

    Func f("f");
    f(x, y) = input(x, y) * input(x, y);

    Func g("g");
    g(x, y) = (f(x, y) + f(x + 1, y)) / 2;

    Func h("h");
    h(x, y) = (f(x, y) + f(x, y + 1)) / 2;

    // Provide estimates on the pipeline output
    g.set_estimate(x, 0, 1000).set_estimate(y, 0, 1000);
    h.set_estimate(x, 0, 1000).set_estimate(y, 0, 1000);

    // Auto-schedule the pipeline
    std::vector<Func> outs;
    outs.push_back(h);
    outs.push_back(g);
    Pipeline p(outs);

    Target target = get_jit_target_from_environment();
    p.apply_autoscheduler(target, {"Mullapudi2016"});

    // Inspect the schedule (only for debugging))
    // h.print_loop_nest();
    // g.print_loop_nest();

    Buffer<uint16_t> out_1(999, 999), out_2(999, 999);

    // Run the schedule
    p.realize({out_1, out_2});

    printf("Success!\n");
    return 0;
}
