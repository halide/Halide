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
    Var x, y, c;

    {
        Func f, g;

        f(x, y, c) = x;
        g(x, y, c) = f(x-1, y+1, c) + f(x, y-1, c);
        f.store_root().compute_at(g, x);

        // Should be able to fold storage in y and c

        g.set_custom_allocator(my_malloc, my_free);

        Buffer<int> im = g.realize(100, 1000, 3);

        size_t expected_size = 101*4*sizeof(int) + sizeof(int);
        if (custom_malloc_size == 0 || custom_malloc_size != expected_size) {
            printf("Scratch space allocated was %d instead of %d\n", (int)custom_malloc_size, (int)expected_size);
            return -1;
        }
    }

    {
        Func f, g;

        f(x, y, c) = x;
        g(x, y, c) = f(x-1, y+1, c) + f(x, y-1, c);
        f.store_root().compute_at(g, x);
        g.specialize(g.output_buffer().width() > 4).vectorize(x, 4);

        // Make sure that storage folding doesn't happen if there are
        // multiple producers of the folded buffer.

        g.set_custom_allocator(my_malloc, my_free);

        Buffer<int> im = g.realize(100, 1000, 3);

        size_t expected_size = 101*1002*3*sizeof(int) + sizeof(int);
        if (custom_malloc_size == 0 || custom_malloc_size != expected_size) {
            printf("Scratch space allocated was %d instead of %d\n", (int)custom_malloc_size, (int)expected_size);
            return -1;
        }
    }

    {
        Func f, g;

        f(x, y) = x;
        g(x, y) = f(x-1, y+1) + f(x, y-1);
        f.store_root().compute_at(g, y).fold_storage(y, 3);
        g.specialize(g.output_buffer().width() > 4).vectorize(x, 4);

        // Make sure that explict storage folding happens, even if
        // there are multiple producers of the folded buffer. Note the
        // automatic storage folding refused to fold this (the case
        // above).

        g.set_custom_allocator(my_malloc, my_free);

        Buffer<int> im = g.realize(100, 1000);

        size_t expected_size = 101*3*sizeof(int) + sizeof(int);
        if (custom_malloc_size == 0 || custom_malloc_size != expected_size) {
            printf("Scratch space allocated was %d instead of %d\n", (int)custom_malloc_size, (int)expected_size);
            return -1;
        }
    }

    {
        custom_malloc_size = 0;
        Func f, g;

        g(x, y) = x * y;
        f(x, y) = g(2*x, 2*y) + g(2*x+1, 2*y+1);

        // Each instance of f uses a non-overlapping 2x2 box of
        // g. Should be able to fold storage of g down to a stack
        // allocation.
        g.compute_at(f, x).store_root();

        f.set_custom_allocator(my_malloc, my_free);

        Buffer<int> im = f.realize(1000, 1000);

        if (custom_malloc_size != 0) {
            printf("There should not have been a heap allocation\n");
            return -1;
        }

        for (int y = 0; y < im.height(); y++) {
            for (int x = 0; x < im.width(); x++) {
                int correct = (2*x) * (2*y) + (2*x+1) * (2*y+1);
                if (im(x, y) != correct) {
                    printf("im(%d, %d) = %d instead of %d\n", x, y, im(x, y), correct);
                    return -1;
                }
            }
        }

    }

    {
        custom_malloc_size = 0;
        Func f, g;

        g(x, y) = x * y;
        f(x, y) = g(x, 2*y) + g(x+3, 2*y+1);

        // Each instance of f uses a non-overlapping 2-scanline slice
        // of g in y, and is a stencil over x. Should be able to fold
        // both x and y.

        g.compute_at(f, x).store_root();

        f.set_custom_allocator(my_malloc, my_free);

        Buffer<int> im = f.realize(1000, 1000);

        if (custom_malloc_size != 0) {
            printf("There should not have been a heap allocation\n");
            return -1;
        }

        for (int y = 0; y < im.height(); y++) {
            for (int x = 0; x < im.width(); x++) {
                int correct = x * (2*y) + (x+3) * (2*y+1);
                if (im(x, y) != correct) {
                    printf("im(%d, %d) = %d instead of %d\n", x, y, im(x, y), correct);
                    return -1;
                }
            }
        }

    }

    {
        custom_malloc_size = 0;
        Func f, g;

        g(x, y) = x * y;
        f(x, y) = g(2*x, y) + g(2*x+1, y+3);

        // Each instance of f uses a non-overlapping 2-scanline slice
        // of g in x, and is a stencil over y. We can't fold in x due
        // to the stencil in y. We need to keep around entire
        // scanlines.

        g.compute_at(f, x).store_root();

        f.set_custom_allocator(my_malloc, my_free);

        Buffer<int> im = f.realize(1000, 1000);

        // Halide allocates one extra scalar, so we account for that.
        size_t expected_size = 2*1002*4*sizeof(int) + sizeof(int);
        if (custom_malloc_size == 0 || custom_malloc_size > expected_size) {
            printf("Scratch space allocated was %d instead of %d\n", (int)custom_malloc_size, (int)expected_size);
            return -1;
        }

        for (int y = 0; y < im.height(); y++) {
            for (int x = 0; x < im.width(); x++) {
                int correct = (2*x) * y + (2*x+1) * (y+3);
                if (im(x, y) != correct) {
                    printf("im(%d, %d) = %d instead of %d\n", x, y, im(x, y), correct);
                    return -1;
                }
            }
        }

    }

    {
        custom_malloc_size = 0;
        Func f, g;

        g(x, y) = x * y;
        f(x, y) = g(x, y);

        Var yo, yi;
        f.bound(y, 0, (f.output_buffer().height()/8)*8).split(y, yo, yi, 8);
        g.compute_at(f, yo).store_root();

        // The split logic shouldn't interfere with the ability to
        // fold f down to an 8-scanline allocation, but it's only
        // correct to fold if we know the output height is a multiple
        // of the split factor.

        f.set_custom_allocator(my_malloc, my_free);

        Buffer<int> im = f.realize(1000, 1000);

        // Halide allocates one extra scalar, so we account for that.
        size_t expected_size = 1000*8*sizeof(int) + sizeof(int);
        if (custom_malloc_size == 0 || custom_malloc_size > expected_size) {
            printf("Scratch space allocated was %d instead of %d\n", (int)custom_malloc_size, (int)expected_size);
            return -1;
        }

        for (int y = 0; y < im.height(); y++) {
            for (int x = 0; x < im.width(); x++) {
                int correct = x*y;
                if (im(x, y) != correct) {
                    printf("im(%d, %d) = %d instead of %d\n", x, y, im(x, y), correct);
                    return -1;
                }
            }
        }

    }

    {
        custom_malloc_size = 0;
        Func f, g;

        g(x, y) = x * y;
        f(x, y) = g(2*x, y) + g(2*x+1, y+2);

        // This is the same test as the above, except the stencil
        // requires 3 rows, of g, not 4. Test explicit storage folding
        // by forcing it to fold over 3 elements. Automatic storage
        // folding would prefer to fold by 4 elements to make modular
        // arithmetic cheaper, but folding by 3 is valid and supported
        // (e.g. if memory usage is a concern.)
        g.compute_at(f, x).store_root().fold_storage(y, 3);

        f.set_custom_allocator(my_malloc, my_free);

        Buffer<int> im = f.realize(1000, 1000);

        // Halide allocates one extra scalar, so we account for that.
        size_t expected_size = 2*1002*3*sizeof(int) + sizeof(int);
        if (custom_malloc_size == 0 || custom_malloc_size > expected_size) {
            printf("Scratch space allocated was %d instead of %d\n", (int)custom_malloc_size, (int)expected_size);
            return -1;
        }

        for (int y = 0; y < im.height(); y++) {
            for (int x = 0; x < im.width(); x++) {
                int correct = (2*x) * y + (2*x+1) * (y+2);
                if (im(x, y) != correct) {
                    printf("im(%d, %d) = %d instead of %d\n", x, y, im(x, y), correct);
                    return -1;
                }
            }
        }

    }

    {
        custom_malloc_size = 0;
        Func f, g;

        g(x, y) = x * y;
        f(x, y) = g(x, y/2) + g(x, y/2+1);

        // The automatic storage folding optimization can't figure
        // this out due to the downsampling. Explicitly fold it.
        g.compute_at(f, x).store_root().fold_storage(y, 2);

        f.set_custom_allocator(my_malloc, my_free);

        Buffer<int> im = f.realize(1000, 1000);

        // Halide allocates one extra scalar, so we account for that.
        size_t expected_size = 1000*2*sizeof(int) + sizeof(int);
        if (custom_malloc_size == 0 || custom_malloc_size > expected_size) {
            printf("Scratch space allocated was %d instead of %d\n", (int)custom_malloc_size, (int)expected_size);
            return -1;
        }

        for (int y = 0; y < im.height(); y++) {
            for (int x = 0; x < im.width(); x++) {
                int correct = (x) * (y/2) + (x) * (y/2 + 1);
                if (im(x, y) != correct) {
                    printf("im(%d, %d) = %d instead of %d\n", x, y, im(x, y), correct);
                    return -1;
                }
            }
        }

    }

    {
        Func f, g;

        f(x, y, c) = x;
        g(x, y, c) = f(x-1, y+1, c) + f(x, y-1, c);
        f.store_root().compute_at(g, y).fold_storage(y, 3);

        // Make sure we can explicitly fold something with an outer
        // loop.

        g.set_custom_allocator(my_malloc, my_free);

        Buffer<int> im = g.realize(100, 1000, 3);

        size_t expected_size = 101*3*sizeof(int) + sizeof(int);
        if (custom_malloc_size == 0 || custom_malloc_size != expected_size) {
            printf("Scratch space allocated was %d instead of %d\n", (int)custom_malloc_size, (int)expected_size);
            return -1;
        }
    }

    printf("Success!\n");
    return 0;
}
