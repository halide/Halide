#include "Halide.h"
#include <stdio.h>

using namespace Halide;

// Override Halide's malloc and free

size_t custom_malloc_size = 0;

void *my_malloc(void *user_context, size_t x) {
    custom_malloc_size = x;
    void *orig = malloc(x + 32);
    void *ptr = (void *)((((size_t)orig + 32) >> 5) << 5);
    ((void **)ptr)[-1] = orig;
    return ptr;
}

void my_free(void *user_context, void *ptr) {
    free(((void **)ptr)[-1]);
}

// An extern stage that copies input -> output
extern "C" HALIDE_EXPORT_SYMBOL int simple_buffer_copy(halide_buffer_t *in, halide_buffer_t *out) {
    if (in->is_bounds_query()) {
        memcpy(in->dim, out->dim, out->dimensions * sizeof(halide_dimension_t));
    } else {
        Halide::Runtime::Buffer<void>(*out).copy_from(Halide::Runtime::Buffer<void>(*in));
    }
    return 0;
}

int main(int argc, char **argv) {
    Var x, y;

    // Test an async producer with dynamic footprint with an outer
    // loop. Uses an external array function to force a dynamic
    // footprint. The test is designed to isolate a possible race
    // condition in the fold accounting. It is currently failing
    // but I have not verified it is the race condition.
    {
        Func f, g, h;

        f(x, y) = x;
        g.define_extern("simple_buffer_copy", {f}, Int(32), 2);
        h(x, y) = g(x - 1, y + 1) + g(x, y - 1);
        f.compute_root();
        g.store_root().compute_at(h, y).fold_storage(g.args()[1], 3).async();

        // Make sure that explict storage folding happens, even if
        // there are multiple producers of the folded buffer. Note the
        // automatic storage folding refused to fold this (the case
        // above).

        h.set_custom_allocator(my_malloc, my_free);

        Buffer<int> im = h.realize({100, 1000});

        size_t expected_size = 101 * 3 * sizeof(int) + sizeof(int);
        if (custom_malloc_size == 0 || custom_malloc_size != expected_size) {
            printf("Scratch space allocated was %d instead of %d\n", (int)custom_malloc_size, (int)expected_size);
            return -1;
        }
    }

    printf("Success!\n");
    return 0;
}
