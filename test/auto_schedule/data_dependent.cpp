#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
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

    p.auto_schedule(target);

    // Inspect the schedule
    g.print_loop_nest();

    // Run the schedule
    Buffer<uint16_t> out = p.realize(input.width() - 1, input.height());

    return 0;
}
