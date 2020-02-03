#include "Halide.h"

using namespace Halide;

int num_vector_stores = 0;
int num_scalar_stores = 0;
int my_trace(void *user_context, const halide_trace_event_t *e) {
    if (e->event == halide_trace_store) {
        if (e->type.lanes > 1) {
            num_vector_stores++;
        } else {
            num_scalar_stores++;
        }
    }
    return 0;
}

int main(int argc, char **argv) {
    Func f;
    Var x;

    f(x) = x;

    const int w = 100, v = 8;
    f.vectorize(x, v, TailStrategy::GuardWithIf);
    const int expected_vector_stores = w / v;
    const int expected_scalar_stores = w % v;

    f.set_custom_trace(&my_trace);
    f.trace_stores();

    Buffer<int> result = f.realize(w);

    if (num_vector_stores != expected_vector_stores) {
        printf("There were %d vector stores instead of %d\n",
               num_vector_stores, expected_vector_stores);
        return -1;
    }

    if (num_scalar_stores != expected_scalar_stores) {
        printf("There were %d scalar stores instead of %d\n",
               num_vector_stores, w % 8);
        return -1;
    }

    for (int i = 0; i < w; i++) {
        if (result(i) != i) {
            printf("result(%d) == %d instead of %d\n",
                   i, result(i), i);
            return -1;
        }
    }

    printf("Success!\n");
    return 0;
}
