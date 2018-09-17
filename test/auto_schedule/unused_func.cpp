#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Var x("x"), y("y");
    Func f("f"), g("g"), h("h");

    g(x) = x;
    g(x) += 10;
    h(x) = x*x;
    f(x) = select(false, g(x + 1), h(x + 1));

    f.estimate(x, 0, 256);

    Target target = get_jit_target_from_environment();
    Pipeline p(f);

    p.auto_schedule(target);

    // Inspect the schedule
    f.print_loop_nest();

    // Run the schedule
    p.realize(256);

    printf("Success!\n");
    return 0;
}
