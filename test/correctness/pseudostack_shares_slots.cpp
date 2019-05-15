#include "Halide.h"

using namespace Halide;

std::vector<size_t> mallocs;

void *my_malloc(void *user_context, size_t x) {
    mallocs.push_back(x);
    void *orig = malloc(x+32);
    void *ptr = (void *)((((size_t)orig + 32) >> 5) << 5);
    ((void **)ptr)[-1] = orig;
    return ptr;
}

void my_free(void *user_context, void *ptr) {
    free(((void**)ptr)[-1]);
}

int main(int argc, char **argv) {
    if (get_jit_target_from_environment().arch == Target::WebAssembly) {
        printf("Skipping test for WebAssembly as the wasm JIT cannot support set_custom_allocator().\n");
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
            next(x) = chain.back()(x-1) + chain.back()(x+1);
            chain.push_back(next);
        }

        Param<int> p;

        Var xo, xi;
        chain.back().split(x, xo, xi, p);
        for (size_t i = 0; i < chain.size() - 1; i++) {
            chain[i].compute_at(chain.back(), xo).store_in(MemoryType::Stack);
        }
        chain.back().set_custom_allocator(my_malloc, my_free);

        for (int sz = 8; sz <= 16; sz += 8) {
            mallocs.clear();
            p.set(sz);
            chain.back().realize(1024);
            size_t sz1 = sz + 2*20 - 1;
            size_t sz2 = sz1 - 2;
            if (mallocs.size() != 2 || mallocs[0] != sz1 || mallocs[1] != sz2) {
                printf("Incorrect allocations: %d %d %d\n", (int)mallocs.size(), (int)mallocs[0], (int)mallocs[1]);
                printf("Expected: 2 %d %d\n", (int)sz1, (int)sz2);
                return -1;
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
                next(x) = chain.back()(x/8);
            } else {
                next(x) = chain.back()(x-1) + chain.back()(x+1);
            }
            chain.push_back(next);
        }

        Param<int> p;

        Var xo, xi;
        chain.back().split(x, xo, xi, p);
        for (size_t i = 0; i < chain.size() - 1; i++) {
            chain[i].compute_at(chain.back(), xo).store_in(MemoryType::Stack);
        }
        chain.back().set_custom_allocator(my_malloc, my_free);

        for (int sz = 64; sz <= 128; sz += 64) {
            mallocs.clear();
            p.set(sz);
            chain.back().realize(1024);
            size_t sz1 = sz/8 + 23;
            size_t sz2 = sz1 - 2;
            size_t sz3 = sz + 19;
            size_t sz4 = sz3 - 2;
            if (mallocs.size() != 4 || mallocs[0] != sz1 || mallocs[1] != sz2 || mallocs[2] != sz3 || mallocs[3] != sz4) {
                printf("Incorrect allocations: %d %d %d %d %d\n", (int)mallocs.size(),
                       (int)mallocs[0], (int)mallocs[1], (int)mallocs[2], (int)mallocs[3]);
                printf("Expected: 4 %d %d %d %d\n", (int)sz1, (int)sz2, (int)sz3, (int)sz4);
                return -1;
            }
        }
    }

    printf("Success!\n");

    return 0;
}
