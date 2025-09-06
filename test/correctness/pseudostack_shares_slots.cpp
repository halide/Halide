#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

namespace {

const int tolerance = 3 * sizeof(int);
std::vector<int> mallocs;

void *my_malloc(JITUserContext *user_context, size_t x) {
    mallocs.push_back((int)x);
    void *orig = malloc(x + 32);
    void *ptr = (void *)((((size_t)orig + 32) >> 5) << 5);
    ((void **)ptr)[-1] = orig;
    return ptr;
}

void my_free(JITUserContext *user_context, void *ptr) {
    free(((void **)ptr)[-1]);
}

}  // namespace

TEST(PseudostackSharesSlots, Basic) {
    if (get_jit_target_from_environment().arch == Target::WebAssembly) {
        GTEST_SKIP() << "WebAssembly JIT does not support custom allocators.";
    }

    // Make a long producer-consumer chain with intermediates
    // allocated on pseudostack. It should simplify down to two
    // allocations.

    {
        Func in;
        Var x;
        in(x) = cast<uint8_t>(x);

        std::vector<Func> chain;
        chain.push_back(in);
        for (int i = 1; i < 20; i++) {
            Func next;
            next(x) = chain.back()(x - 1) + chain.back()(x + 1);
            chain.push_back(next);
        }

        Param<int> p;

        Var xo, xi;
        chain.back().split(x, xo, xi, p);
        for (size_t i = 0; i < chain.size() - 1; i++) {
            chain[i].compute_at(chain.back(), xo).store_in(MemoryType::Stack);
        }
        chain.back().jit_handlers().custom_malloc = my_malloc;
        chain.back().jit_handlers().custom_free = my_free;

        // Use sizes that trigger actual heap allocations
        for (int sz = 20000; sz <= 20016; sz += 8) {
            mallocs.clear();
            p.set(sz);
            chain.back().realize({sz * 4});
            int sz1 = sz + 2 * 20 - 1;
            int sz2 = sz1 - 2;
            ASSERT_EQ(mallocs.size(), 2) << "Expected 2 allocations, got " << mallocs.size();
            ASSERT_LT(std::abs(mallocs[0] - sz1), tolerance) << "First allocation size " << mallocs[0] << " too far from expected " << sz1;
            ASSERT_LT(std::abs(mallocs[1] - sz2), tolerance) << "Second allocation size " << mallocs[1] << " too far from expected " << sz2;
        }
    }

    // Test a scenario involving a reallocation due to reuse with increased size
    {
        Func in;
        Var x;
        in(x) = cast<uint8_t>(x);

        std::vector<Func> chain;
        chain.push_back(in);
        for (int i = 1; i < 20; i++) {
            Func next;
            if (i == 10) {
                next(x) = chain.back()(x / 4);
            } else {
                next(x) = chain.back()(x - 1) + chain.back()(x + 1);
            }
            chain.push_back(next);
        }

        Param<int> p;

        Var xo, xi;
        chain.back().split(x, xo, xi, p);
        for (size_t i = 0; i < chain.size() - 1; i++) {
            chain[i].compute_at(chain.back(), xo).store_in(MemoryType::Stack);
        }
        chain.back().jit_handlers().custom_malloc = my_malloc;
        chain.back().jit_handlers().custom_free = my_free;

        for (int sz = 160000; sz <= 160128; sz += 64) {
            mallocs.clear();
            p.set(sz);
            chain.back().realize({sz * 4});
            int sz1 = sz / 4 + 23;
            int sz2 = sz1 - 2;
            int sz3 = sz + 19;
            int sz4 = sz3 - 2;
            ASSERT_EQ(mallocs.size(), 4) << "Expected 4 allocations, got " << mallocs.size();
            ASSERT_LT(std::abs(mallocs[0] - sz1), tolerance) << "First allocation size " << mallocs[0] << " too far from expected " << sz1;
            ASSERT_LT(std::abs(mallocs[1] - sz2), tolerance) << "Second allocation size " << mallocs[1] << " too far from expected " << sz2;
            ASSERT_LT(std::abs(mallocs[2] - sz3), tolerance) << "Third allocation size " << mallocs[2] << " too far from expected " << sz3;
            ASSERT_LT(std::abs(mallocs[3] - sz4), tolerance) << "Fourth allocation size " << mallocs[3] << " too far from expected " << sz4;
        }
    }
}
