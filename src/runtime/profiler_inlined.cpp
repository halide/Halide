#include "HalideRuntime.h"
#include "runtime_internal.h"

extern "C" {

WEAK __attribute__((always_inline)) int halide_profiler_set_current_func(halide_profiler_state *state, int tok, int t) {
    // Do a volatile store to discourage llvm from reordering it.
    volatile int *ptr = &(state->current_func);
    *ptr = tok + t;
    return 0;
}

}
