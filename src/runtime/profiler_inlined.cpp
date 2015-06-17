#include "HalideRuntime.h"
#include "runtime_internal.h"
#include "profiler_state.h"

extern "C" {

WEAK __attribute__((always_inline)) int halide_profiler_set_current_func(profiler_state *state, int tok, int t) {
    state->current_func = tok + t;
    return 0;
}

}
