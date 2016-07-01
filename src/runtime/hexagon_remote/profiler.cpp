#include <HalideRuntime.h>

extern "C" halide_profiler_state *halide_profiler_get_state() {
    static halide_profiler_state hvx_profiler_state;
    return &hvx_profiler_state;
}
