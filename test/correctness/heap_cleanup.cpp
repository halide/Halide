#include "Halide.h"
#include <atomic>
#include <gtest/gtest.h>

using namespace Halide;

namespace {
// Check that assertion failures free allocations appropriately
std::atomic<int> malloc_count{0};
std::atomic<int> free_count{0};

void *my_malloc(JITUserContext *user_context, size_t x) {
    malloc_count++;
    void *orig = malloc(x + 32);
    void *ptr = (void *)((((size_t)orig + 32) >> 5) << 5);
    ((void **)ptr)[-1] = orig;
    return ptr;
}

void my_free(JITUserContext *user_context, void *ptr) {
    free_count++;
    free(((void **)ptr)[-1]);
}

bool error_occurred = false;
void my_error_handler(JITUserContext *user_context, const char *) {
    error_occurred = true;
}

class HeapCleanupTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (get_jit_target_from_environment().arch == Target::WebAssembly) {
            GTEST_SKIP() << "WebAssembly JIT does not support custom allocators.";
        }

        malloc_count = 0;
        free_count = 0;
        error_occurred = false;
    }
};
}  // namespace

TEST_F(HeapCleanupTest, AssertionFailureFreesAllocations) {
    Func f, g, h;
    Var x;

    f(x) = x;
    f.compute_root();
    g(x) = f(x) + 1;
    g.compute_root();
    h(x) = g(x) + 1;

    // This should fail an assertion at runtime after f has been allocated
    int g_size = 100000;
    g.bound(x, 0, g_size);

    h.jit_handlers().custom_malloc = my_malloc;
    h.jit_handlers().custom_free = my_free;
    h.jit_handlers().custom_error = my_error_handler;

    Buffer<int> im = h.realize({g_size + 100});

    ASSERT_TRUE(error_occurred) << "Expected an error to occur during realization";
    ASSERT_EQ(malloc_count, free_count) << "malloc_count=" << malloc_count << " free_count=" << free_count;
}
