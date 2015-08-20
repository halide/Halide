#include "HalideRuntime.h"
#include "runtime_internal.h"

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

}
