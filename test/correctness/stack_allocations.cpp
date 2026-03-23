#include "Halide.h"
#include <stdio.h>

using namespace Halide;

extern "C" {
void *my_malloc(JITUserContext *ctx, size_t sz) {
    printf("There weren't supposed to be heap allocations!\n");
    exit(1);
    return nullptr;
}

void my_free(JITUserContext *ctx, void *ptr) {
    printf("There weren't supposed to be heap allocations!\n");
    exit(1);
}
}

int main(int argc, char **argv) {
    if (get_jit_target_from_environment().arch == Target::WebAssembly) {
        printf("[SKIP] WebAssembly JIT does not support custom allocators.\n");
        return 0;
    }

    Func f, g, h;
    Var x, y;

    f(x, y) = x + y;
    g(x, y) = f(x - 1, y + 1) * f(x + 1, y - 1);
    h(x, y) = g(x + 1, y + 1) + g(x - 1, y - 1);

    f.compute_at(h, x);
    g.compute_at(h, x);
    Var xi, yi;
    h.tile(x, y, xi, yi, 4, 3).vectorize(xi);

    // f and g should both do stack allocations
    h.jit_handlers().custom_malloc = my_malloc;
    h.jit_handlers().custom_free = my_free;

    h.realize({10, 10});

    printf("Success!\n");
    return 0;
}
