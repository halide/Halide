#include "Halide.h"
#include <stdio.h>

using namespace Halide;

// Override Halide's malloc and free

bool custom_malloc_called = false;
bool custom_free_called = false;

void *my_malloc(JITUserContext *user_context, size_t x) {
    custom_malloc_called = true;
    void *orig = malloc(x + 32);
    void *ptr = (void *)((((size_t)orig + 32) >> 5) << 5);
    ((void **)ptr)[-1] = orig;
    return ptr;
}

void my_free(JITUserContext *user_context, void *ptr) {
    custom_free_called = true;
    free(((void **)ptr)[-1]);
}

void *mischievous_malloc(JITUserContext *user_context, size_t x) {
    fprintf(stderr, "This should never get called\n");
    abort();
    return nullptr;
}

void run_test(bool use_callable) {
    custom_malloc_called = false;
    custom_free_called = false;

    Func f, g;
    Var x;

    f(x) = x;
    g(x) = f(x);
    f.compute_root();

    g.jit_handlers().custom_malloc = my_malloc;
    g.jit_handlers().custom_free = my_free;

    constexpr int size = 100000;
    if (!use_callable) {
        Buffer<int> im = g.realize({size});
    } else {
        Callable c = g.compile_to_callable({});

        // Changing g's handlers shouldn't affect any existing Callables
        g.jit_handlers().custom_malloc = mischievous_malloc;

        Buffer<int> im(size);
        int r = c(im);
        _halide_user_assert(r == 0);
    }

    assert(custom_malloc_called);
    assert(custom_free_called);
}

int main(int argc, char **argv) {
    if (get_jit_target_from_environment().arch == Target::WebAssembly) {
        printf("[SKIP] WebAssembly JIT does not support custom allocators.\n");
        return 0;
    }

    run_test(false);
    run_test(true);

    printf("Success!\n");
    return 0;
}
