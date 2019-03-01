#include "HalideRuntime.h"

extern "C" {

#ifdef BITS_64
extern bool QueryPerformanceCounter(int64_t *);
extern bool QueryPerformanceFrequency(int64_t *);
extern void Sleep(int);
#else
extern __stdcall bool QueryPerformanceCounter(int64_t *);
extern __stdcall bool QueryPerformanceFrequency(int64_t *);
extern __stdcall void Sleep(int);
#endif

WEAK bool halide_reference_clock_inited = false;
WEAK int64_t halide_reference_clock = 0;
WEAK int64_t halide_clock_frequency = 1;

WEAK int halide_start_clock(void *user_context) {
    // Guard against multiple calls
    if (!halide_reference_clock_inited) {
        QueryPerformanceCounter(&halide_reference_clock);
        QueryPerformanceFrequency(&halide_clock_frequency);
        halide_reference_clock_inited = true;
    }
    return 0;
}

WEAK int64_t halide_current_time_ns(void *user_context) {
    int64_t clock;
    QueryPerformanceCounter(&clock);
    clock -= halide_reference_clock;
    double ns_per_tick = 1000000000.0 / halide_clock_frequency;
    return (int64_t)(ns_per_tick * clock);
}

}
