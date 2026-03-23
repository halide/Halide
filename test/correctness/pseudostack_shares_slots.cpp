#include "Halide.h"

using namespace Halide;

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

int main(int argc, char **argv) {
    if (get_jit_target_from_environment().arch == Target::WebAssembly) {
        printf("[SKIP] WebAssembly JIT does not support custom allocators.\n");
        return 0;
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
            if (mallocs.size() != 2 ||
                std::abs(mallocs[0] - sz1) > tolerance ||
                std::abs(mallocs[1] - sz2) > tolerance) {
                printf("Incorrect allocations: %d %d %d\n", (int)mallocs.size(), (int)mallocs[0], (int)mallocs[1]);
                printf("Expected: 2 %d %d\n", (int)sz1, (int)sz2);
                return 1;
            }
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
            if (mallocs.size() != 4 ||
                std::abs(mallocs[0] - sz1) > tolerance ||
                std::abs(mallocs[1] - sz2) > tolerance ||
                std::abs(mallocs[2] - sz3) > tolerance ||
                std::abs(mallocs[3] - sz4) > tolerance) {
                printf("Incorrect allocations: %d %d %d %d %d\n", (int)mallocs.size(),
                       mallocs[0], mallocs[1], mallocs[2], mallocs[3]);
                printf("Expected: 4 %d %d %d %d\n", sz1, sz2, sz3, sz4);
                return 1;
            }
        }
    }

    printf("Success!\n");

    return 0;
}
