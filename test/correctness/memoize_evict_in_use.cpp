#include "Halide.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace Halide;

// Freed blocks are filled with a fixed pattern and kept around rather than
// returned to the system, so that reads and writes of freed memory can be
// detected.
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

constexpr uint64_t eviction_key = 42;
int call_count = 0;

extern "C" HALIDE_EXPORT_SYMBOL int count_calls(halide_buffer_t *out) {
    if (!out->is_bounds_query()) {
        call_count++;
        Halide::Runtime::Buffer<uint8_t>(*out).fill(17);
    }
    return 0;
}

// Evicts the memoized input while it is still in use by this pipeline, as
// another thread could, and then reads it.
extern "C" HALIDE_EXPORT_SYMBOL int evict_and_copy(halide_buffer_t *in, halide_buffer_t *out) {
    if (in->is_bounds_query()) {
        for (int i = 0; i < out->dimensions; i++) {
            in->dim[i] = out->dim[i];
        }
    } else if (!out->is_bounds_query()) {
        Internal::JITSharedRuntime::memoization_cache_evict(eviction_key);
        Halide::Runtime::Buffer<uint8_t>(*out).copy_from(Halide::Runtime::Buffer<uint8_t>(*in));
    }
    return 0;
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

    Var x("x"), y("y");
    Func count("count"), f("f"), g("g");
    count.define_extern("count_calls", {}, UInt(8), 2);
    f(x, y) = count(x, y);
    count.compute_root();
    f.compute_root().memoize(EvictionKey((int)eviction_key));
    g.define_extern("evict_and_copy", {f}, UInt(8), 2);

    for (int i = 0; i < 3; i++) {
        Buffer<uint8_t> out = g.realize({64, 64});
        for (int yy = 0; yy < 64; yy++) {
            for (int xx = 0; xx < 64; xx++) {
                if (out(xx, yy) != 17) {
                    printf("out(%d, %d) = %d instead of 17\n", xx, yy, out(xx, yy));
                    return 1;
                }
            }
        }
        if (!freed_blocks_untouched()) {
            printf("Memory was written after being freed\n");
            return 1;
        }
        // f was evicted during the previous run, so it is recomputed.
        if (call_count != i + 1) {
            printf("call_count = %d instead of %d\n", call_count, i + 1);
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}
