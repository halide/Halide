#include "HalideRuntime.h"

extern "C" {

WEAK_INLINE int halide_profiler_set_current_func(halide_profiler_instance_state *instance, int func, int *sampling_token) {
    if (sampling_token == nullptr || *sampling_token == 0) {

        // Use empty volatile asm blocks to prevent code motion. Otherwise
        // llvm reorders or elides the stores.
        volatile int *ptr = &(instance->current_func);
        // clang-format off
        asm volatile ("":::);
        *ptr = func;
        asm volatile ("":::);
        // clang-format on
    }
    return 0;
}

// Called once we're sure we're not in bounds query code
WEAK_INLINE int halide_profiler_enable_instance(halide_profiler_instance_state *instance) {
    instance->should_collect_statistics = 1;
    return 0;
}

// Invariant: shared xor local, and both are either 0 or 1. 0 means acquired.
WEAK_INLINE int halide_profiler_acquire_sampling_token(int32_t *shared, int32_t *local) {
    int32_t result;
    int32_t one = 1;
    __atomic_exchange(shared, &one, &result, __ATOMIC_ACQUIRE);
    *local = result;
    return 0;
}

WEAK_INLINE int halide_profiler_release_sampling_token(int32_t *shared, int32_t *local) {
    if (*local == 0) {
        int32_t value = 0;
        __atomic_store(shared, &value, __ATOMIC_RELEASE);
        *local = 1;
    }
    return 0;
}

WEAK_INLINE int halide_profiler_init_sampling_token(int32_t *sampling_token, int val) {
    *sampling_token = val;
    return 0;
}

WEAK_INLINE int halide_profiler_incr_active_threads(halide_profiler_instance_state *instance) {
    return __atomic_fetch_add(&(instance->active_threads), 1, __ATOMIC_SEQ_CST);
}

WEAK_INLINE int halide_profiler_decr_active_threads(halide_profiler_instance_state *instance) {
    return __atomic_fetch_sub(&(instance->active_threads), 1, __ATOMIC_SEQ_CST);
}
}
