#include <Halide.h>
#include <stdio.h>

using namespace Halide;

void my_trace(const char *function, int event_type,
              int type_code, int bits, int width,
              int value_index, const void *value,
              int num_int_args, const int *int_args) {
    // The schedule implies that f will be stored from 0 to 8
    if (event_type == 2) {
        if (int_args[1] < 8) {
            printf("Bounds on realization of f were supposed to be >= [0, 9]\n"
                   "Instead they are: %d %d\n", int_args[0], int_args[1]);
            exit(-1);
        }
    }
}

int main(int argc, char **argv) {
    Func f("f"), g("g");
    Var x("x");
    f(x) = x;
    RDom r(17, 1);
    f(x) = r;
    f.store_root();

    g(x) = f(x) + f(x+1);
    f.compute_at(g, x);

    Var xo("xo"), xi("xi");
    f.split(x, xo, xi, 8);

    f.trace_realizations().trace_stores();

    g.set_custom_trace(&my_trace);
    g.bound(x, 0, 2);
    g.output_buffer().set_bounds(0, 0, 2);
    g.realize(2);

    printf("Success!\n");

    return 0;

}
