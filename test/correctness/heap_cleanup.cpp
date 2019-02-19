#include "Halide.h"
#include <stdio.h>
#include <atomic>

using namespace Halide;

// Check that assertion failures free allocations appropriately

std::atomic<int> malloc_count;
std::atomic<int> free_count;

void *my_malloc(void *user_context, size_t x) {
    malloc_count++;
    void *orig = malloc(x+32);
    void *ptr = (void *)((((size_t)orig + 32) >> 5) << 5);
    ((void **)ptr)[-1] = orig;
    return ptr;
}

void my_free(void *user_context, void *ptr) {
    free_count++;
    free(((void**)ptr)[-1]);
}

bool error_occurred = false;
void my_error_handler(void *user_context, const char *) {
    error_occurred = true;
}

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (target.has_feature(Target::JavaScript)) {
        // TODO: Add JavaScript custom malloc/free.
        printf("Skipping heap_cleanup test for JavaScript as it depends on custom malloc/free.\n");
        return 0;
    }

    Func f, g, h;
    Var x;

    malloc_count = 0;
    free_count = 0;

    f(x) = x;
    f.compute_root();
    g(x) = f(x)+1;
    g.compute_root();
    h(x) = g(x)+1;

    // This should fail an assertion at runtime after f has been allocated
    int g_size = 100000;
    g.bound(x, 0, g_size);

    h.set_custom_allocator(my_malloc, my_free);
    h.set_error_handler(my_error_handler);

    Buffer<int> im = h.realize(g_size+100);

    printf("%d %d\n", (int)malloc_count, (int)free_count);

    assert(error_occurred && (int)malloc_count == (int)free_count);

    printf("Success!\n");
    return 0;
}
