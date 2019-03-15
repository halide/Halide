#include "Halide.h"
#include <stdio.h>

using namespace Halide;

size_t expected_allocation = 0;

void *my_malloc(void *user_context, size_t x) {
    if (x != expected_allocation) {
        printf("Error! Expected allocation of %zu bytes, got %zu bytes\n", expected_allocation, x);
        exit(-1);
    }
    return malloc(x);
}

void my_free(void *user_context, void *ptr) {
    free(ptr);
}

int main(int argc, char **argv) {
    if (get_jit_target_from_environment().arch == Target::WebAssembly) {
        printf("Skipping test for WebAssembly as the wasm JIT cannot support set_custom_allocator().\n");
        return 0;
    }

    Target target = get_jit_target_from_environment();
    if (target.has_feature(Target::Debug)) {
        // the runtime debug adds some debug payload to each allocation,
        // so the 'expected_allocation' is unlikely to be a match.
        printf("Skipping test because runtime debug is active\n");
        return 0;
    }
    Var x, y, c;
    Func f("f"), g;

    f(x, y, c) = 1;
    g(x, y, c) = f(x, y, c);

    f.compute_root().reorder_storage(c, x, y);
    g.set_custom_allocator(my_malloc, my_free);

    // Without any storage alignment, we should expect an allocation
    // that is the product of the extents of the realization (plus one
    // for the magical extra Halide element).
    int W = 10;
    int H = 11;
    expected_allocation = (3*W*H + 1)*sizeof(int);

    g.realize(W, H, 3);

    int x_alignment = 16;
    f.align_storage(x, x_alignment);

    // We've aligned the x dimension, make sure the allocation reflects this.
    int W_aligned = (W + x_alignment - 1) & ~(x_alignment - 1);
    expected_allocation = (W_aligned*H*3 + 1)*sizeof(int);

    // Force g to clear it's cache...
    g.compute_root();
    g.realize(W, H, 3);

    printf("Success!\n");
    return 0;
}
