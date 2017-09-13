#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    int W = 800;
    int H = 1200;

    Buffer<uint16_t> input(W, H);

    for (int y = 0; y < input.height(); y++) {
        for (int x = 0; x < input.width(); x++) {
            input(x, y) = rand() & 0xfff;
        }
    }

    Var x("x"), y("y"), c("c");

    Func in_b("in_b");
    in_b = BoundaryConditions::repeat_edge(input);

    int win_size = 15;
    RDom w(-win_size, win_size, -win_size, win_size);
    Func f("f");
    f(x, y) = sum(in_b(x + w.x, y + w.y), "sum1")/1024;

    Func g("g");
    g(x, y) = sum(f(x + w.x, y + w.y), "sum2")/1024;

    // Provide estimates on the pipeline output
    g.estimate(x, 0, input.width()).estimate(y, 0, input.height());

    // Pick a schedule
    Target target = get_jit_target_from_environment();
    Pipeline p(g);

    p.auto_schedule(target);

    // Inspect the schedule
    g.print_loop_nest();

    // Run the schedule
    Buffer<uint16_t> out = p.realize(input.width(), input.height());

    printf("Success!\n");
    return 0;
}
