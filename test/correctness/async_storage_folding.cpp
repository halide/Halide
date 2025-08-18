#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

namespace {

// Override Halide's malloc and free
size_t custom_malloc_size = 0;

void *my_malloc(JITUserContext *user_context, size_t x) {
    custom_malloc_size = x;
    void *orig = malloc(x + 32);
    void *ptr = (void *)((((size_t)orig + 32) >> 5) << 5);
    ((void **)ptr)[-1] = orig;
    return ptr;
}

void my_free(JITUserContext *user_context, void *ptr) {
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

}  // namespace

TEST(AsyncStorageFoldingTest, DISABLED_DynamicFootprintWithExternArrayFunc) {
    // TODO: This test deadlocks. See issue #3293.
    GTEST_SKIP() << "This test deadlocks. See issue #3293.";
    
    Var x, y;

    // Test an async producer with dynamic footprint with an outer
    // loop. Uses an external array function to force a dynamic
    // footprint. The test is designed to isolate a possible race
    // condition in the fold accounting.
    Func f, g, h;

    f(x, y) = x;
    g.define_extern("simple_buffer_copy", {f}, Int(32), 2);
    h(x, y) = g(x - 1, y + 1) + g(x, y - 1);
    f.compute_root();
    g.store_root().compute_at(h, y).fold_storage(g.args()[1], 3).async();

    // Make sure that explicit storage folding happens, even if
    // there are multiple producers of the folded buffer. Note the
    // automatic storage folding refused to fold this (the case
    // above).

    h.jit_handlers().custom_malloc = my_malloc;
    h.jit_handlers().custom_free = my_free;

    Buffer<int> im = h.realize({100, 1000});

    size_t expected_size = 101 * 3 * sizeof(int) + sizeof(int);
    EXPECT_NE(custom_malloc_size, 0) << "No custom malloc occurred";
    EXPECT_EQ(custom_malloc_size, expected_size) << "Scratch space allocated was " << custom_malloc_size << " instead of " << expected_size;
}
