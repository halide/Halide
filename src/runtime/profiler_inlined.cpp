#include "HalideRuntime.h"

extern "C" {

WEAK_INLINE int halide_profiler_set_current_func(halide_profiler_state *state, int tok, int t) {
    // Use empty volatile asm blocks to prevent code motion. Otherwise
    // llvm reorders or elides the stores.
    volatile int *ptr = &(state->current_func);
    // clang-format off
    asm volatile ("":::);
    *ptr = tok + t;
    asm volatile ("":::);
    // clang-format on
    return 0;
}

WEAK_INLINE int halide_profiler_incr_active_threads(halide_profiler_state *state) {
    volatile int *ptr = &(state->active_threads);
    // clang-format off
    asm volatile ("":::);
    int ret = __sync_fetch_and_add(ptr, 1);
    asm volatile ("":::);
    // clang-format on
    return ret;
}

WEAK_INLINE int halide_profiler_decr_active_threads(halide_profiler_state *state) {
    volatile int *ptr = &(state->active_threads);
    // clang-format off
    asm volatile ("":::);
    int ret = __sync_fetch_and_sub(ptr, 1);
    asm volatile ("":::);
    // clang-format on
    return ret;
}
}
