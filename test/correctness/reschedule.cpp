#include "Halide.h"
#include <stdio.h>

using namespace Halide;

bool vector_store = false, scalar_store = false;

// A trace that checks for vector and scalar stores
int my_trace(JITUserContext *user_context, const halide_trace_event_t *ev) {

    if (ev->event == halide_trace_store) {
        if (ev->type.lanes > 1) {
            vector_store = true;
        } else {
            scalar_store = true;
        }
    }
    return 0;
}

int main(int argc, char **argv) {
    Func f;
    Var x;

    f(x) = x;
    f.jit_handlers().custom_trace = &my_trace;
    f.trace_stores();

    Buffer<int> result_1 = f.realize({10});

    f.vectorize(x, 4);

    Buffer<int> result_2 = f.realize({10});

    // There should have been vector stores and scalar stores.
    if (!vector_store || !scalar_store) {
        printf("There should have been vector and scalar stores\n");
        return 1;
    }

    printf("Success!\n");
    return 0;
}
