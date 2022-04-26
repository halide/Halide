#include "HalideRuntime.h"

extern "C" {

typedef int32_t zx_status_t;
typedef int64_t zx_time_t;
typedef int64_t zx_duration_t;

zx_time_t zx_clock_get_monotonic();

zx_time_t zx_deadline_after(zx_duration_t nanoseconds);
zx_status_t zx_nanosleep(zx_time_t deadline);

WEAK bool halide_reference_clock_inited = false;
WEAK zx_time_t halide_reference_clock;

WEAK int halide_start_clock(void *user_context) {
    // Guard against multiple calls
    if (!halide_reference_clock_inited) {
        halide_reference_clock = zx_clock_get_monotonic();
        halide_reference_clock_inited = true;
    }
    return 0;
}

WEAK int64_t halide_current_time_ns(void *user_context) {
    // It is an error to call halide_current_time_ns() if halide_start_clock() has never been called
    halide_debug_assert(user_context, halide_reference_clock_inited);

    return zx_clock_get_monotonic() - halide_reference_clock;
}

WEAK void halide_sleep_ms(void *user_context, int ms) {
    zx_nanosleep(zx_deadline_after(ms * 1000));
}
}
