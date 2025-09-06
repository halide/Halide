#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

namespace {
int mallocs = 0;

void *my_malloc(JITUserContext *, size_t sz) {
    mallocs++;
    return (uint8_t *)malloc(sz);
}

void my_free(JITUserContext *, void *ptr) {
    free(ptr);
}

class StoreInTest : public ::testing::TestWithParam<std::tuple<MemoryType, MemoryType, MemoryType>> {
};
}  // namespace

TEST_P(StoreInTest, CheckMemoryTypes) {
    if (get_jit_target_from_environment().arch == Target::WebAssembly) {
        GTEST_SKIP() << "WebAssembly JIT does not support custom allocators.";
    }

    const auto [t1, t2, t3] = GetParam();
    Var x;

    // By default, small constant-sized allocations, or
    // allocations that can be bounded with a small constant size,
    // go on the stack. Other allocations go on the heap.

    Func f1, f2, f3;
    f1(x) = x;
    f1.compute_root().store_in(t1);
    f2(x) = x;
    f2.compute_root().store_in(t2);
    f3(x) = x;
    f3.compute_root().store_in(t3);

    Func f;
    Param<bool> p;
    f(x) = (f1(0) + f1(1)) + f2(select(p, 0, 2)) + f2(0) + f3(x % 1000);

    p.set(true);

    int expected_mallocs = ((t1 == MemoryType::Heap ? 1 : 0) +
                            (t2 == MemoryType::Heap ? 1 : 0) +
                            (t3 == MemoryType::Heap ? 1 : 0));

    mallocs = 0;
    f.jit_handlers().custom_malloc = my_malloc;
    f.jit_handlers().custom_free = my_free;
    ASSERT_NO_THROW(f.realize({1024}));
    EXPECT_EQ(mallocs, expected_mallocs) << "Wrong number of mallocs for " << t1 << ", " << t2 << ", " << t3;
}

static auto memory_types =
    ::testing::Values(MemoryType::Auto, MemoryType::Stack, MemoryType::Heap);
INSTANTIATE_TEST_SUITE_P(
    MemoryTypes, StoreInTest, ::testing::Combine(memory_types, memory_types, memory_types));
