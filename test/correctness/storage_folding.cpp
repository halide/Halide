#include "Halide.h"
#include <stdio.h>

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

#ifdef _WIN32
#define DLLEXPORT __declspec(dllexport)
#else
#define DLLEXPORT
#endif

// An extern stage that copies input -> output
extern "C" DLLEXPORT int simple_buffer_copy(halide_buffer_t *in, halide_buffer_t *out) {
    if (in->is_bounds_query()) {
        memcpy(in->dim, out->dim, out->dimensions * sizeof(halide_dimension_t));
    } else {
        Halide::Runtime::Buffer<void>(*out).copy_from(Halide::Runtime::Buffer<void>(*in));
    }
    return 0;
}

// An extern stage accesses the input in a non-monotonic way in the y dimension.
extern "C" DLLEXPORT int zigzag_buffer_copy(halide_buffer_t *in, halide_buffer_t *out) {
    if (in->is_bounds_query()) {
        memcpy(in->dim, out->dim, out->dimensions * sizeof(halide_dimension_t));
        int y_min = in->dim[1].min;
        int y_max = y_min + in->dim[1].extent - 1;
        y_min &= 63;
        if (y_min >= 32) {
            y_min = 63 - y_min;
        }
        y_max &= 63;
        if (y_max >= 32) {
            y_max = 63 - y_max;
        }
        if (y_min > y_max) {
            std::swap(y_min, y_max);
        }
        in->dim[1].min = y_min;
        in->dim[1].extent = y_max - y_min + 1;
    } else {
        // This extern stage is only used to see if it produces an
        // expected bounds error, so just fill it with a sentinel value.
        Halide::Runtime::Buffer<int>(*out).fill(99);
    }
    return 0;
}

bool error_occurred;
void expected_error(void *, const char *msg) {
    // Emitting "error.*:" to stdout or stderr will cause CMake to report the
    // test as a failure on Windows, regardless of error code returned,
    // hence the abbreviation to "err".
    printf("Expected err: %s\n", msg);
    error_occurred = true;
}

void realize_and_expect_error(Func f, int w, int h) {
    error_occurred = false;
    f.set_error_handler(expected_error);
    f.realize(w, h);
    if (!error_occurred) {
        printf("Expected an error!\n");
        abort();
    }
}

int main(int argc, char **argv) {
    // TODO: See if this can be tested somehow with JavaScript.
    Target target = get_jit_target_from_environment();
    if (target.has_feature(Target::JavaScript)) {
        printf("Skipping storage_folding test for JavaScript as allocations don't go through allocator in the same way.\n");
        return 0;
    }

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

    for (bool interleave : {false, true}) {
        Func f, g;

        f(x, y, c) = x;
        g(x, y, c) = f(x-1, y+1, c) + f(x, y-1, c);
        f.store_root().compute_at(g, y).fold_storage(y, 3);

        if (interleave) {
            f.reorder(c, x, y).reorder_storage(c, x, y);
            g.reorder(c, x, y).reorder_storage(c, x, y);
        }

        // Make sure we can explicitly fold something with an outer
        // loop.

        g.set_custom_allocator(my_malloc, my_free);

        Buffer<int> im = g.realize(100, 1000, 3);

        size_t expected_size;
        if (interleave) {
            expected_size = 101*3*3*sizeof(int) + sizeof(int);
        } else {
            expected_size = 101*3*sizeof(int) + sizeof(int);
        }
        if (custom_malloc_size == 0 || custom_malloc_size != expected_size) {
            printf("Scratch space allocated was %d instead of %d\n", (int)custom_malloc_size, (int)expected_size);
            return -1;
        }
    }

    {
        // Fold the storage of the output of an extern stage
        Func f, g, h;
        Var x, y;
        f(x, y) = x + y;
        g.define_extern("simple_buffer_copy", {f}, Int(32), 2);
        h(x, y) = g(x, y);

        f.compute_root();
        g.store_root().compute_at(h, y).fold_storage(g.args()[1], 8);
        h.compute_root();

        Buffer<int> out = h.realize(64, 64);
        out.for_each_element([&](int x, int y) {
                if (out(x, y) != x + y) {
                    printf("out(%d, %d) = %d instead of %d\n", x, y, out(x, y), x + y);
                    abort();
                }
            });
    }

    {
        // Fold the storage of an input to an extern stage
        Func f, g, h;
        Var x, y;
        f(x, y) = x + y;
        g.define_extern("simple_buffer_copy", {f}, Int(32), 2);
        h(x, y) = g(x, y);

        f.store_root().compute_at(h, y).fold_storage(y, 8);
        g.compute_at(h, y);
        h.compute_root();

        Buffer<int> out = h.realize(64, 64);
        out.for_each_element([&](int x, int y) {
                if (out(x, y) != x + y) {
                    printf("out(%d, %d) = %d instead of %d\n", x, y, out(x, y), x + y);
                    abort();
                }
            });
    }

    // Now we check some error cases.

    {
        // Fold the storage of an input to an extern stage, with a too-small fold factor.
        Func f, g, h;
        Var x, y;
        f(x, y) = x + y;
        g.define_extern("simple_buffer_copy", {f}, Int(32), 2);
        h(x, y) = g(x, y);

        f.store_root().compute_at(h, y).fold_storage(y, 4);
        g.compute_at(h, y);
        Var yi;
        h.compute_root().split(y, y, yi, 8);

        realize_and_expect_error(h, 64, 64);
    }

    {
        // Fold the storage of an input to an extern stage, where one
        // of the regions required by the extern stage will overlap a
        // fold boundary (thanks to ShiftInwards).
        Func f, g, h;
        Var x, y;
        f(x, y) = x + y;
        g.define_extern("simple_buffer_copy", {f}, Int(32), 2);
        h(x, y) = g(x, y);

        f.store_root().compute_at(h, y).fold_storage(y, 4);
        g.compute_at(h, y);
        Var yi;
        h.compute_root().split(y, y, yi, 4);

        realize_and_expect_error(h, 64, 7);
    }

    {
        // Fold the storage of an input to an extern stage, where the
        // extern stage moves non-monotonically on the input.
        Func f, g, h;
        Var x, y;
        f(x, y) = x + y;
        g.define_extern("zigzag_buffer_copy", {f}, Int(32), 2);
        h(x, y) = g(x, y);

        f.store_root().compute_at(h, y).fold_storage(y, 4);
        g.compute_at(h, y);
        Var yi;
        h.compute_root().split(y, y, yi, 2);

        realize_and_expect_error(h, 64, 64);
    }

    {
        // Fold the storage of the output of an extern stage, where
        // one of the regions written crosses a fold boundary.
        Func f, g, h;
        Var x, y;
        f(x, y) = x + y;
        g.define_extern("simple_buffer_copy", {f}, Int(32), 2);
        h(x, y) = g(x, y);

        f.compute_root();
        g.store_root().compute_at(h, y).fold_storage(g.args()[1], 4);
        Var yi;
        h.compute_root().split(y, y, yi, 4);

        realize_and_expect_error(h, 64, 7);
    }


    {
        // Check a case which used to be problematic
        Func input, a, b, c, output;
        Var xo, yo, line, chunk;

        input(x, y) = x;
        a(x, y) = input(x, y);
        b(x, y) = select(y % 2 == 0, a(x, y / 2), a(x, y / 2 + 1));

        c = lambda(x, y, b(x, y));

        output(x, y) = c(x, y);


        output
            .bound(y, 0, 64)
            .compute_root()
            .split(y, line, y, 2, TailStrategy::RoundUp)
            .split(line, chunk, line, 32, TailStrategy::RoundUp);

        c
            .tile(x, y, xo, yo, x, y, 2, 2, TailStrategy::RoundUp)
            .compute_at(output, line)
            .store_at(output, chunk);

        a
            .tile(x, y, xo, yo, x, y, 2, 2, TailStrategy::RoundUp)
            .compute_at(c, yo)
            .store_at(output, chunk)
            .fold_storage(y, 4)  // <<-- this should be OK, but previously it sometimes wanted 6.
            .align_bounds(y, 2);

        Buffer<int> im = output.realize(64, 64);
    }

    printf("Success!\n");
    return 0;
}
