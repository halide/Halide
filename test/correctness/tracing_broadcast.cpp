#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int my_trace(void *user_context, const halide_trace_event_t *e) {
    if (e->event == halide_trace_store) {
        for (int i = 0; i < e->type.lanes; ++i) {
            int val = ((const int *)(e->value))[i];
            if (val != 1234567890) {
                printf("All values stored should have been 1234567890\n"
                       "Instead they are: %d\n", val);
                exit(-1);
            }
        }
    }
    return 0;
}

int main(int argc, char **argv) {
    Func f("f");
    Var x("x"), y("y");
    f(x, y) = 1234567890;
    f.vectorize(x, 8);

    f.trace_stores();
    f.set_custom_trace(&my_trace);
    f.realize(8, 8);

    printf("Success!\n");

    return 0;

}
