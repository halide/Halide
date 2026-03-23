#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int my_trace(JITUserContext *user_context, const halide_trace_event_t *e) {

    if (e->event == 2) {  // begin realization
        if (e->coordinates[1] != 4) {
            printf("Realization of f was supposed to be 4-wide\n");
            exit(1);
        }
    }
    return 0;
}

int main(int argc, char **argv) {
    Func f("f"), g("g"), h("h");
    Var x;

    // One pixel of this is needed.
    f(x) = x;
    f.compute_root();

    // One pixel of this is needed, but four will be computed, loading
    // four values from f(x), so the allocation of f(x) had better be
    // 4-wide.
    g(x) = f(x) + 1;
    g.compute_root().vectorize(x, 4);

    // One pixel of this is needed.
    h(x) = g(x) + 2;
    h.output_buffer().dim(0).set_bounds(0, 1);

    f.trace_realizations();
    h.jit_handlers().custom_trace = &my_trace;
    h.realize({1});

    printf("Success!\n");
    return 0;
}
