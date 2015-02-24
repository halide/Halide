#include <stdio.h>
#include "Halide.h"

using namespace Halide;

// Override Halide's malloc and free

size_t custom_malloc_size = 0;

void *my_malloc(void *user_context, size_t x) {
    custom_malloc_size = x;
    void *orig = malloc(x+32);
    void *ptr = (void *)((((size_t)orig + 32) >> 5) << 5);
    ((void **)ptr)[-1] = orig;
    return ptr;
}

void my_free(void *user_context, void *ptr) {
    free(((void**)ptr)[-1]);
}

int main(int argc, char **argv) {
    Func f, g;
    Var x, y;

    f(x, y) = x;
    g(x, y) = f(x-1, y) + f(x, y-1);
    f.store_root().compute_at(g, x);

    g.set_custom_allocator(my_malloc, my_free);

    Image<int> im = g.realize(1000, 1000);

    // Should fold by a factor of two, but sliding window analysis makes it round up to 4.
    if (custom_malloc_size == 0 || custom_malloc_size > 1002*4*sizeof(int)) {
        printf("Scratch space allocated was %d instead of %d\n", (int)custom_malloc_size, (int)(1002*4*sizeof(int)));
        return -1;
    }

    printf("Success!\n");
    return 0;
}
