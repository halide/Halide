#include "Halide.h"
#include <stdio.h>

using namespace Halide;

// Not threadsafe!!!
size_t mem_limit = (size_t)-1;
size_t total_allocated = 0;

// Ussing a lookaside instead of increasing the size of the block to hold the
// allocation size keeps the malloc behavior the same with regard to alignement
// and bug behaviors, etc. Cheap enough to be good in testing.
std::map<void *, size_t> allocation_sizes;

extern "C" void *test_malloc(JITUserContext *user_context, size_t x) {
    if (total_allocated + x > mem_limit)
        return nullptr;

    void *result = malloc(x);
    if (result != nullptr) {
        total_allocated += x;
        allocation_sizes[result] = x;
    }

    return result;
}

extern "C" void test_free(JITUserContext *user_context, void *ptr) {
    total_allocated -= allocation_sizes[ptr];
    allocation_sizes.erase(ptr);
    free(ptr);
}

bool error_occurred = false;
extern "C" void handler(JITUserContext *user_context, const char *msg) {
    printf("%s\n", msg);
    error_occurred = true;
}

int main(int argc, char **argv) {
    if (get_jit_target_from_environment().arch == Target::WebAssembly) {
        printf("[SKIP] WebAssembly JIT does not support custom allocators.\n");
        return 0;
    }

    const int big = 1 << 26;
    Var x;
    std::vector<Func> funcs;
    funcs.push_back(lambda(x, cast<uint8_t>(x)));
    for (size_t i = 0; i < 10; i++) {
        Func f;
        f(x) = funcs[i](0) + funcs[i](big);
        funcs[i].compute_at(f, x);
        funcs.push_back(f);
    }

    // Limit ourselves to two stages worth of address space
    mem_limit = big << 1;

    funcs[funcs.size() - 1].jit_handlers().custom_malloc = test_malloc;
    funcs[funcs.size() - 1].jit_handlers().custom_free = test_free;
    funcs[funcs.size() - 1].jit_handlers().custom_error = handler;
    funcs[funcs.size() - 1].realize({1});

    if (!error_occurred) {
        printf("There should have been an error\n");
        return 1;
    }

    printf("Success!\n");
    return 0;
}
