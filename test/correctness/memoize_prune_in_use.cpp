#include "Halide.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace Halide;

// Freed blocks are filled with a fixed pattern and kept around rather than
// returned to the system, so that any later write through a dangling pointer
// into the memoization cache can be detected.
struct BlockHeader {
    void *orig;
    size_t size;
};

constexpr size_t alignment = 128;
constexpr uint8_t poison = 0xa5;
constexpr int max_freed = 4096;
void *freed[max_freed];
int num_freed = 0;

void *custom_malloc(JITUserContext *user_context, size_t x) {
    void *orig = malloc(x + alignment + sizeof(BlockHeader));
    if (!orig) {
        return nullptr;
    }
    size_t addr = (size_t)orig + sizeof(BlockHeader);
    addr = (addr + alignment - 1) & ~(alignment - 1);
    void *ptr = (void *)addr;
    ((BlockHeader *)ptr)[-1] = {orig, x};
    return ptr;
}

void custom_free(JITUserContext *user_context, void *ptr) {
    if (!ptr) {
        return;
    }
    memset(ptr, poison, ((BlockHeader *)ptr)[-1].size);
    if (num_freed < max_freed) {
        freed[num_freed++] = ptr;
    } else {
        free(((BlockHeader *)ptr)[-1].orig);
    }
}

bool freed_blocks_untouched() {
    for (int i = 0; i < num_freed; i++) {
        const uint8_t *p = (const uint8_t *)freed[i];
        size_t size = ((const BlockHeader *)freed[i])[-1].size;
        for (size_t j = 0; j < size; j++) {
            if (p[j] != poison) {
                return false;
            }
        }
    }
    return true;
}

int main(int argc, char **argv) {
    if (get_jit_target_from_environment().arch == Target::WebAssembly) {
        printf("[SKIP] WebAssembly JIT does not support custom allocators.\n");
        return 0;
    }

    // Cache entries are allocated without a user context, so the default
    // handlers are the ones that see them.
    JITHandlers handlers;
    handlers.custom_malloc = custom_malloc;
    handlers.custom_free = custom_free;
    Internal::JITSharedRuntime::set_default_handlers(handlers);

    const int W = 256;
    Var x("x"), y("y");
    Param<int> p;
    Func a("a"), b("b"), c("c"), d("d"), out("out");
    a(x, y) = cast<uint8_t>(x + y);
    b(x, y) = cast<uint8_t>(x + p);
    c(x, y) = b(x, y) + 1;
    d(x, y) = cast<uint8_t>(y + p);
    out(x, y) = a(x, y) + c(x, y) + d(x, y);

    // a stays in use until out is computed, while b is released once c is
    // done. With room for only two of these in the cache, b gets pruned
    // while a, which is less recently used, is still in use.
    a.compute_root().memoize();
    b.compute_root().memoize();
    c.compute_root().memoize();
    d.compute_root().memoize();
    Internal::JITSharedRuntime::memoization_cache_set_size(2 * W * W);

    for (int i = 0; i < 20; i++) {
        p.set(i);
        Buffer<uint8_t> r = out.realize({W, W});
        for (int yy = 0; yy < W; yy++) {
            for (int xx = 0; xx < W; xx++) {
                uint8_t correct = (uint8_t)((uint8_t)(xx + yy) +
                                            (uint8_t)((uint8_t)(xx + i) + 1) +
                                            (uint8_t)(yy + i));
                if (r(xx, yy) != correct) {
                    printf("r(%d, %d) = %d instead of %d\n", xx, yy, r(xx, yy), correct);
                    return 1;
                }
            }
        }
        if (!freed_blocks_untouched()) {
            printf("Memory was written after being freed\n");
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}
