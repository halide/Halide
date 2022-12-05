#include "Halide.h"
#include <stdio.h>

#include <list>

using namespace Halide;

extern "C" HALIDE_EXPORT_SYMBOL std::list<int> *list_create(int) {
    return new std::list<int>();
}
HalideExtern_1(std::list<int> *, list_create, int);

extern "C" HALIDE_EXPORT_SYMBOL std::list<int> *list_maybe_insert(std::list<int> *list, bool insert, int value) {
    if (insert) {
        list->push_back(value);
    }
    return list;
}
HalideExtern_3(std::list<int> *, list_maybe_insert, std::list<int> *, bool, int);

int main(int argc, char **argv) {
    if (get_jit_target_from_environment().arch == Target::WebAssembly) {
        printf("[SKIP] WebAssembly JIT does not support passing arbitrary pointers to/from HalideExtern code.\n");
        return 0;
    }

    // Compute the list of factors of all numbers < 100
    Func factors;
    Var x;

    // Ideally this would only iterate up to the square root of x, but
    // we don't have dynamic reduction bounds yet.
    RDom r(1, 99);

    // Create an std::list for each result
    factors(x) = list_create(x);

    // Because Halide::select evaluates both paths, we need to move
    // the condition into the C function.
    factors(x) = list_maybe_insert(factors(x), x % r == 0, r);

    Buffer<std::list<int> *> result = factors.realize({100});

    // Inspect the results for correctness
    for (int i = 0; i < 100; i++) {
        std::list<int> *list = result(i);
        // printf("Factors of %d: ", i);
        for (std::list<int>::iterator iter = list->begin(); iter != list->end(); iter++) {
            int factor = *iter;
            if (i % factor) {
                printf("Error: %d is not a factor of %d\n", factor, i);
                return -1;
            }
            // printf("%d ", factor);
        }
        // printf("\n");
        delete list;
    }

    printf("Success!\n");
    return 0;
}
