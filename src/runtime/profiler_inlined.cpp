#include "HalideRuntime.h"

extern "C" {

WEAK_INLINE int halide_profiler_set_current_func(halide_profiler_state *state, int pipeline, int func, int *sampling_token) {
    if (sampling_token == nullptr || *sampling_token == 0) {

        // Use empty volatile asm blocks to prevent code motion. Otherwise
        // llvm reorders or elides the stores.
        volatile int *ptr = &(state->current_func);
        // clang-format off
        asm volatile ("":::);
        *ptr = pipeline + func;
        asm volatile ("":::);
        // clang-format on
    }
    return 0;
}

// Invariant: shared xor local, and both are either 0 or 1. 0 means acquired.
WEAK_INLINE int halide_profiler_acquire_sampling_token(int32_t *shared, int32_t *local) {
    *local = __sync_lock_test_and_set(shared, 1);
    return 0;
}

WEAK_INLINE int halide_profiler_release_sampling_token(int32_t *shared, int32_t *local) {
    if (*local == 0) {
        __sync_lock_release(shared);
        *local = 1;
    }
    return 0;
}

WEAK_INLINE int halide_profiler_init_sampling_token(int32_t *sampling_token, int val) {
    *sampling_token = val;
    return 0;
}

WEAK_INLINE int halide_profiler_incr_active_threads(halide_profiler_state *state) {
    return __sync_fetch_and_add(&(state->active_threads), 1);
}

WEAK_INLINE int halide_profiler_decr_active_threads(halide_profiler_state *state) {
    return __sync_fetch_and_sub(&(state->active_threads), 1);
}
}
