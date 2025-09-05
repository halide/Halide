#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

namespace {
extern "C" {
void *my_malloc(JITUserContext *ctx, size_t sz) {
    ADD_FAILURE() << "There shouldn't be any heap allocations!";
    return nullptr;
}

void my_free(JITUserContext *ctx, void *ptr) {
    FAIL() << "There shouldn't be any heap allocations!";
}
}
}  // namespace

TEST(StackAllocationsTest, StackAllocations) {
    if (get_jit_target_from_environment().arch == Target::WebAssembly) {
        GTEST_SKIP() << "WebAssembly JIT does not support custom allocators.";
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

    h.jit_handlers().custom_malloc = my_malloc;
    h.jit_handlers().custom_free = my_free;

    ASSERT_NO_THROW(h.realize({10, 10}));
}
