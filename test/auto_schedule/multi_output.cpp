#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
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
    g(x, y) = (f(x, y) + f(x + 1, y))/2;

    Func h("h");
    h(x, y) = (f(x, y) + f(x, y+1))/2;

    // Provide estimates on the pipeline output
    g.estimate(x, 0, 1000).estimate(y, 0, 1000);
    h.estimate(x, 0, 1000).estimate(y, 0, 1000);

    // Auto-schedule the pipeline
    std::vector<Func> outs;
    outs.push_back(h);
    outs.push_back(g);
    Pipeline test(outs);

    Target target = get_jit_target_from_environment();
    test.auto_schedule(target);

    // Inspect the schedule
    h.print_loop_nest();
    g.print_loop_nest();

    Buffer<uint16_t> out_1(999, 999), out_2(999, 999);

    // Run the schedule
    test.realize({out_1, out_2});

    printf("Success!\n");
    return 0;
}
