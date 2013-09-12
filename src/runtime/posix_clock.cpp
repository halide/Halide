#include <stdint.h>

#ifndef _STRUCT_TIMEVAL
#define _STRUCT_TIMEVAL

#ifdef _LP64
typedef int64_t sec_t;
#else
typedef int32_t sec_t;
#endif

// OSX always uses an int32 for the usec field
#if defined(_LP64) && !defined(__APPLE__)
typedef int64_t usec_t;
#else
typedef int32_t usec_t;
#endif

struct timeval {
    sec_t tv_sec;
    usec_t tv_usec;
};

#endif

extern "C" {

extern int gettimeofday(timeval *tv, void *);

WEAK bool halide_reference_clock_inited = false;
WEAK timeval halide_reference_clock;

WEAK int halide_start_clock() {
    // Guard against multiple calls
    if (!halide_reference_clock_inited) {
      gettimeofday(&halide_reference_clock, NULL);
      halide_reference_clock_inited = true;
    }
    return 0;
}

// clock_gettime() is preferred over gettimeofday(), but OSX
// doesn't provide the former. (Use linux_clock.cpp to use clock_gettime(),
// which will provide actual nanosecond accuracy.)
WEAK int64_t halide_current_time_ns() {
    timeval now;
    gettimeofday(&now, NULL);
    int64_t d = int64_t(now.tv_sec - halide_reference_clock.tv_sec)*1000000;
    int64_t ud = int64_t(now.tv_usec) - int64_t(halide_reference_clock.tv_usec);
    return (d + ud) * 1000;
}

}
