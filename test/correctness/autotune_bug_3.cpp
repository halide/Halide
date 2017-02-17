#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int my_trace(void *user_context, const halide_trace_event_t *e) {
    // The schedule implies that f will be stored from 0 to 8
    if (e->event == 2 && std::string(e->func) == "f") {
        if (e->coordinates[1] < 8) {
            printf("Bounds on realization of f were supposed to be >= [0, 8]\n"
                   "Instead they are: %d %d\n",
                   e->coordinates[0], e->coordinates[1]);
            exit(-1);
        }
    }
    return 0;
}

int main(int argc, char **argv) {
    Func f("f"), g("g");
    Var x("x");
    f(x) = x;
    f.store_root();

    g(x) = f(x) + f(x + 1);
    f.compute_at(g, x);

    Var xo("xo"), xi("xi");
    f.split(x, xo, xi, 8);

    f.trace_realizations().trace_stores();

    g.set_custom_trace(&my_trace);
    g.bound(x, 0, 2);
    g.output_buffer().dim(0).set_bounds(0, 2);
    g.realize(2);

    printf("Success!\n");

    return 0;
}
