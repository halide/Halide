#include "HalideRuntime.h"

#include "posix_timeval.h"

namespace Halide {
namespace Runtime {
namespace Internal {
WEAK bool halide_reference_clock_inited = false;
WEAK timeval halide_reference_clock;
}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

extern "C" {

extern int gettimeofday(timeval *tv, void *);

WEAK int halide_start_clock(void *user_context) {
    // Guard against multiple calls
    if (!halide_reference_clock_inited) {
        gettimeofday(&halide_reference_clock, nullptr);
        halide_reference_clock_inited = true;
    }
    return 0;
}

// clock_gettime() is preferred over gettimeofday(), but OSX
// doesn't provide the former. (Use linux_clock.cpp to use clock_gettime(),
// which will provide actual nanosecond accuracy.)
WEAK int64_t halide_current_time_ns(void *user_context) {
    // It is an error to call halide_current_time_ns() if halide_start_clock() has never been called
    halide_debug_assert(user_context, halide_reference_clock_inited);

    timeval now;
    gettimeofday(&now, nullptr);
    int64_t d = int64_t(now.tv_sec - halide_reference_clock.tv_sec) * 1000000;
    int64_t ud = int64_t(now.tv_usec) - int64_t(halide_reference_clock.tv_usec);
    return (d + ud) * 1000;
}

extern int usleep(int);
WEAK void halide_sleep_ms(void *user_context, int ms) {
    usleep(ms * 1000);
}
}
