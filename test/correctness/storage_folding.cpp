#include "Halide.h"
#include <stdio.h>

#include <set>

using namespace Halide;

// Override Halide's malloc and free
const int tolerance = 3 * sizeof(int);
std::set<size_t> custom_malloc_sizes;

void *my_malloc(JITUserContext *user_context, size_t x) {
    custom_malloc_sizes.insert(x);
    void *orig = malloc(x + 32);
    void *ptr = (void *)((((size_t)orig + 32) >> 5) << 5);
    ((void **)ptr)[-1] = orig;
    return ptr;
}

void my_free(JITUserContext *user_context, void *ptr) {
    free(((void **)ptr)[-1]);
}

bool check_expected_malloc(size_t expected) {
    for (size_t i : custom_malloc_sizes) {
        if (std::abs((int)i - (int)expected) <= tolerance) {
            return true;
        }
    }
    printf("Expected an allocation of size %d (tolerance %d). Got instead:\n", (int)expected, tolerance);
    for (size_t i : custom_malloc_sizes) {
        printf("  %d\n", (int)i);
    }
    return false;
}

bool check_expected_mallocs(const std::vector<size_t> &expected) {
    for (size_t i : expected) {
        if (!check_expected_malloc(i)) {
            return false;
        }
    }
    return true;
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

// An extern stage accesses the input in a non-monotonic way in the y dimension.
extern "C" HALIDE_EXPORT_SYMBOL int zigzag_buffer_copy(halide_buffer_t *in, halide_buffer_t *out) {
    if (in->is_bounds_query()) {
        memcpy(in->dim, out->dim, out->dimensions * sizeof(halide_dimension_t));

        // An intentionally nasty mapping from y coords of the output to y coords of the input:
        auto coord_map =
            [](int y) {
                // Reverse the bottom 8 bits
                int new_y = y & ~255;
                for (int i = 0; i < 8; i++) {
                    if (y & (7 - i)) {
                        new_y |= (1 << i);
                    }
                }
                return new_y;
            };

        // Just manually take a min/max over all scanlines of the output
        int in_y_min = coord_map(out->dim[1].min);
        int in_y_max = in_y_min;
        for (int out_y = out->dim[1].min + 1; out_y < out->dim[1].min + out->dim[1].extent; out_y++) {
            int in_y = coord_map(out_y);
            in_y_min = std::min(in_y_min, in_y);
            in_y_max = std::max(in_y_max, in_y);
        }
        in->dim[1].min = in_y_min;
        in->dim[1].extent = in_y_max - in_y_min + 1;
    } else {
        // This extern stage is only used to see if it produces an
        // expected bounds error, so just fill it with a sentinel value.
        Halide::Runtime::Buffer<int>(*out).fill(99);
    }
    return 0;
}

bool error_occurred;
void expected_error(JITUserContext *, const char *msg) {
    // Emitting "error.*:" to stdout or stderr will cause CMake to report the
    // test as a failure on Windows, regardless of error code returned,
    // hence the abbreviation to "err".
    printf("Expected err: %s\n", msg);
    error_occurred = true;
}

void realize_and_expect_error(Func f, int w, int h) {
    error_occurred = false;
    f.jit_handlers().custom_error = expected_error;
    f.realize({w, h});
    if (!error_occurred) {
        printf("Expected an error!\n");
        abort();
    }
}

int main(int argc, char **argv) {
    if (get_jit_target_from_environment().arch == Target::WebAssembly) {
        printf("[SKIP] WebAssembly JIT does not support custom allocators.\n");
        return 0;
    }

    Var x, y, c;

    // Every allocation in this test wants to go through the custom allocator above.
    JITHandlers handlers;
    handlers.custom_malloc = my_malloc;
    handlers.custom_free = my_free;
    Internal::JITSharedRuntime::set_default_handlers(handlers);

    {
        Func f, g;

        f(x, y, c) = x;
        g(x, y, c) = f(x - 1, y + 1, c) + f(x, y - 1, c);
        f.store_root().compute_at(g, x);

        // Should be able to fold storage in y and c

        Buffer<int> im = g.realize({100, 1000, 3});

        size_t expected_size = 101 * 4 * sizeof(int);
        if (!check_expected_mallocs({expected_size})) {
            return 1;
        }
    }

    {
        Func f, g;

        f(x, y, c) = x;
        g(x, y, c) = f(x - 1, y + 1, c) + f(x, y - 1, c);
        f.store_root().compute_at(g, x);
        g.specialize(g.output_buffer().width() > 4).vectorize(x, 4);

        // Make sure that storage folding doesn't happen if there are
        // multiple producers of the folded buffer.

        Buffer<int> im = g.realize({100, 1000, 3});

        size_t expected_size = 101 * 1002 * 3 * sizeof(int);
        if (!check_expected_mallocs({expected_size})) {
            return 1;
        }
    }

    {
        Func f, g;

        f(x, y) = x;
        g(x, y) = f(x - 1, y + 1) + f(x, y - 1);
        f.store_root().compute_at(g, y).fold_storage(y, 3);
        g.specialize(g.output_buffer().width() > 4).vectorize(x, 4);

        // Make sure that explict storage folding happens, even if
        // there are multiple producers of the folded buffer. Note the
        // automatic storage folding refused to fold this (the case
        // above).

        Buffer<int> im = g.realize({100, 1000});

        size_t expected_size = 101 * 3 * sizeof(int);
        if (!check_expected_mallocs({expected_size})) {
            return 1;
        }
    }

    {
        custom_malloc_sizes.clear();
        Func f, g;

        g(x, y) = x * y;
        f(x, y) = g(2 * x, 2 * y) + g(2 * x + 1, 2 * y + 1);

        // Each instance of f uses a non-overlapping 2x2 box of
        // g. Should be able to fold storage of g down to a stack
        // allocation.
        g.compute_at(f, x).store_root();

        Buffer<int> im = f.realize({1000, 1000});

        if (!custom_malloc_sizes.empty()) {
            printf("There should not have been a heap allocation\n");
            return 1;
        }

        for (int y = 0; y < im.height(); y++) {
            for (int x = 0; x < im.width(); x++) {
                int correct = (2 * x) * (2 * y) + (2 * x + 1) * (2 * y + 1);
                if (im(x, y) != correct) {
                    printf("im(%d, %d) = %d instead of %d\n", x, y, im(x, y), correct);
                    return 1;
                }
            }
        }
    }

    {
        custom_malloc_sizes.clear();
        Func f, g;

        g(x, y) = x * y;
        f(x, y) = g(x, 2 * y) + g(x + 3, 2 * y + 1);

        // Each instance of f uses a non-overlapping 2-scanline slice
        // of g in y, and is a stencil over x. Should be able to fold
        // both x and y.

        g.compute_at(f, x).store_root();

        Buffer<int> im = f.realize({1000, 1000});

        if (!custom_malloc_sizes.empty()) {
            printf("There should not have been a heap allocation\n");
            return 1;
        }

        for (int y = 0; y < im.height(); y++) {
            for (int x = 0; x < im.width(); x++) {
                int correct = x * (2 * y) + (x + 3) * (2 * y + 1);
                if (im(x, y) != correct) {
                    printf("im(%d, %d) = %d instead of %d\n", x, y, im(x, y), correct);
                    return 1;
                }
            }
        }
    }

    {
        custom_malloc_sizes.clear();
        Func f, g;

        g(x, y) = x * y;
        f(x, y) = g(2 * x, y) + g(2 * x + 1, y + 3);

        // Each instance of f uses a non-overlapping 2-scanline slice
        // of g in x, and is a stencil over y. We can't fold in x due
        // to the stencil in y. We need to keep around entire
        // scanlines.

        g.compute_at(f, x).store_root();

        Buffer<int> im = f.realize({1000, 1000});

        size_t expected_size = 2 * 1000 * 4 * sizeof(int);
        if (!check_expected_mallocs({expected_size})) {
            return 1;
        }

        for (int y = 0; y < im.height(); y++) {
            for (int x = 0; x < im.width(); x++) {
                int correct = (2 * x) * y + (2 * x + 1) * (y + 3);
                if (im(x, y) != correct) {
                    printf("im(%d, %d) = %d instead of %d\n", x, y, im(x, y), correct);
                    return 1;
                }
            }
        }
    }

    {
        custom_malloc_sizes.clear();
        Func f, g;

        g(x, y) = x * y;
        f(x, y) = g(x, y);

        Var yo, yi;
        f.bound(y, 0, (f.output_buffer().height() / 8) * 8).split(y, yo, yi, 8);
        g.compute_at(f, yo).store_root();

        // The split logic shouldn't interfere with the ability to
        // fold f down to an 8-scanline allocation, but it's only
        // correct to fold if we know the output height is a multiple
        // of the split factor.

        Buffer<int> im = f.realize({1000, 1000});

        size_t expected_size = 1000 * 8 * sizeof(int);
        if (!check_expected_mallocs({expected_size})) {
            return 1;
        }

        for (int y = 0; y < im.height(); y++) {
            for (int x = 0; x < im.width(); x++) {
                int correct = x * y;
                if (im(x, y) != correct) {
                    printf("im(%d, %d) = %d instead of %d\n", x, y, im(x, y), correct);
                    return 1;
                }
            }
        }
    }

    {
        custom_malloc_sizes.clear();
        Func f, g;

        g(x, y) = x * y;
        f(x, y) = g(2 * x, y) + g(2 * x + 1, y + 2);

        // This is the same test as the above, except the stencil
        // requires 3 rows, of g, not 4. Test explicit storage folding
        // by forcing it to fold over 3 elements. Automatic storage
        // folding would prefer to fold by 4 elements to make modular
        // arithmetic cheaper, but folding by 3 is valid and supported
        // (e.g. if memory usage is a concern.)
        g.compute_at(f, x).store_root().fold_storage(y, 3);

        Buffer<int> im = f.realize({1000, 1000});

        size_t expected_size = 2 * 1000 * 3 * sizeof(int);
        if (!check_expected_mallocs({expected_size})) {
            return 1;
        }

        for (int y = 0; y < im.height(); y++) {
            for (int x = 0; x < im.width(); x++) {
                int correct = (2 * x) * y + (2 * x + 1) * (y + 2);
                if (im(x, y) != correct) {
                    printf("im(%d, %d) = %d instead of %d\n", x, y, im(x, y), correct);
                    return 1;
                }
            }
        }
    }

    {
        custom_malloc_sizes.clear();
        Func f, g;

        // This is tricky due to upsampling.
        g(x, y) = x * y;
        f(x, y) = g(x, y / 2) + g(x, y / 2 + 1);

        g.compute_at(f, x).store_root();

        Buffer<int> im = f.realize({1000, 1000});

        size_t expected_size = 1000 * 2 * sizeof(int);
        if (!check_expected_mallocs({expected_size})) {
            return 1;
        }

        for (int y = 0; y < im.height(); y++) {
            for (int x = 0; x < im.width(); x++) {
                int correct = (x) * (y / 2) + (x) * (y / 2 + 1);
                if (im(x, y) != correct) {
                    printf("im(%d, %d) = %d instead of %d\n", x, y, im(x, y), correct);
                    return 1;
                }
            }
        }
    }

    {
        custom_malloc_sizes.clear();
        Func f, g, h;

        // Two stages of upsampling is even trickier.
        h(x, y) = x * y;
        g(x, y) = h(x, y / 2) + h(x, y / 2 + 1);
        f(x, y) = g(x, y / 2) + g(x, y / 2 + 1);

        h.compute_at(f, y).store_root().fold_storage(y, 4);
        g.compute_at(f, y).store_root().fold_storage(y, 2);

        Buffer<int> im = f.realize({1000, 1000});

        // Halide allocates one extra scalar, so we account for that.
        size_t expected_size_g = 1000 * 4 * sizeof(int) + sizeof(int);
        size_t expected_size_h = 1000 * 2 * sizeof(int) + sizeof(int);
        if (!check_expected_mallocs({expected_size_g, expected_size_h})) {
            return 1;
        }

        for (int y = 0; y < im.height(); y++) {
            for (int x = 0; x < im.width(); x++) {
                auto correct_h = [](int x, int y) { return x * y; };
                auto correct_g = [=](int x, int y) { return correct_h(x, y / 2) + correct_h(x, y / 2 + 1); };
                auto correct_f = [=](int x, int y) { return correct_g(x, y / 2) + correct_g(x, y / 2 + 1); };
                if (im(x, y) != correct_f(x, y)) {
                    printf("im(%d, %d) = %d instead of %d\n", x, y, im(x, y), correct_f(x, y));
                    return 1;
                }
            }
        }
    }

    for (bool interleave : {false, true}) {
        Func f, g;

        f(x, y, c) = x;
        g(x, y, c) = f(x - 1, y + 1, c) + f(x, y - 1, c);
        f.store_root().compute_at(g, y).fold_storage(y, 3);

        if (interleave) {
            f.reorder(c, x, y).reorder_storage(c, x, y);
            g.reorder(c, x, y).reorder_storage(c, x, y);
        }

        // Make sure we can explicitly fold something with an outer
        // loop.

        Buffer<int> im = g.realize({100, 1000, 3});

        size_t expected_size;
        if (interleave) {
            expected_size = 101 * 3 * 3 * sizeof(int);
        } else {
            expected_size = 101 * 3 * sizeof(int);
        }
        if (!check_expected_mallocs({expected_size})) {
            return 1;
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

        Buffer<int> out = h.realize({64, 64});
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

        Buffer<int> out = h.realize({64, 64});
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

        Buffer<int> im = output.realize({64, 64});
    }

    printf("Success!\n");
    return 0;
}
