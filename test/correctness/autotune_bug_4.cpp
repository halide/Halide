#include <Halide.h>
#include <stdio.h>

using namespace Halide;

void my_trace(const char *function, int event_type,
              int type_code, int bits, int width,
              int value_index, const void *value,
              int num_int_args, const int *int_args) {
    // The schedule implies that f and g will be stored from 0 to 7
    if (event_type == 2) {
        if (int_args[1] < 7) {
            printf("Bounds on realization were supposed to be = [0, 7]\n"
                   "Instead they are: %d %d\n", int_args[0], int_args[1]);
            exit(-1);
        }
    }
}

int main(int argc, char **argv) {
    Func f("f"), g("g"), h("h");
    Var x("x");

    f(x) = x;
    g(x) = f(x);
    h(x) = g(x) + g(x+1);

    Var xo("xo"), xi("xi");
    f.split(x, xo, xi, 4);
    g.split(x, xo, xi, 5);
    h.split(x, xo, xi, 6);
    f.compute_at(h, xo);
    g.compute_at(h, xo);
    g.store_root();

    f.trace_realizations().trace_stores().trace_loads();
    g.trace_realizations().trace_stores().trace_loads();

    h.set_custom_trace(&my_trace);
    h.bound(x, 0, 6);
    h.realize(6);

    printf("Success!\n");

    return 0;

}
