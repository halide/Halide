#include "HalideRuntime.h"

extern "C" {

WEAK __attribute__((always_inline)) int halide_profiler_set_current_func(halide_profiler_state *state, int tok, int t) {
    // Use empty volatile asm blocks to prevent code motion. Otherwise
    // llvm reorders or elides the stores.
    volatile int *ptr = &(state->current_func);
    asm volatile ("":::);
    *ptr = tok + t;
    asm volatile ("":::);
    return 0;
}

WEAK __attribute__((always_inline)) int halide_profiler_incr_active_threads(halide_profiler_state *state) {
    volatile int *ptr = &(state->active_threads);
    asm volatile ("":::);
    int ret = __sync_fetch_and_add(ptr, 1);
    asm volatile ("":::);
    return ret;
}

WEAK __attribute__((always_inline)) int halide_profiler_decr_active_threads(halide_profiler_state *state) {
    volatile int *ptr = &(state->active_threads);
    asm volatile ("":::);
    int ret = __sync_fetch_and_sub(ptr, 1);
    asm volatile ("":::);
    return ret;
}

}
