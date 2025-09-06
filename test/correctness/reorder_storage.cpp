#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

namespace {
// Backends allocate up to 3 extra elements.
int tolerance = 3 * sizeof(int);
int expected_allocation = 0;

void *my_malloc(JITUserContext *user_context, size_t x) {
    if (std::abs((int)x - expected_allocation) > tolerance) {
        ADD_FAILURE() << "Expected allocation of " << expected_allocation << " bytes, got " << x << " bytes (tolerance " << tolerance << ")";
    }
    return malloc(x);
}

void my_free(JITUserContext *user_context, void *ptr) {
    free(ptr);
}
}  // namespace

TEST(ReorderStorageTest, ReorderStorage) {
    if (get_jit_target_from_environment().arch == Target::WebAssembly) {
        GTEST_SKIP() << "WebAssembly JIT does not support custom allocators.";
    }

    Target target = get_jit_target_from_environment();
    if (target.has_feature(Target::Debug)) {
        // the runtime debug adds some debug payload to each allocation,
        // so the 'expected_allocation' is unlikely to be a match.
        GTEST_SKIP() << "Test incompatible with debug runtime.";
    }
    Var x, y, c;
    Func f("f"), g;

    f(x, y, c) = 1;
    g(x, y, c) = f(x, y, c);

    f.compute_root().reorder_storage(c, x, y);
    g.jit_handlers().custom_malloc = my_malloc;
    g.jit_handlers().custom_free = my_free;

    // Without any storage alignment, we should expect an allocation
    // that is the product of the extents of the realization.
    int W = 10;
    int H = 11;
    expected_allocation = 3 * W * H * sizeof(int);

    ASSERT_NO_THROW(g.realize({W, H, 3}));

    int x_alignment = 16;
    f.align_storage(x, x_alignment);

    // We've aligned the x dimension, make sure the allocation reflects this.
    int W_aligned = (W + x_alignment - 1) & ~(x_alignment - 1);
    expected_allocation = W_aligned * H * 3 * sizeof(int);

    // Force g to clear it's cache...
    g.compute_root();
    ASSERT_NO_THROW(g.realize({W, H, 3}));
}
