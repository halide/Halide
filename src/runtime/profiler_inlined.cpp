#include "HalideRuntime.h"
#include "runtime_internal.h"

extern "C" {

WEAK __attribute__((always_inline)) int halide_profiler_set_current_func(halide_profiler_state *state, int tok, int t) {
    state->current_func = tok + t;
    return 0;
}

}
