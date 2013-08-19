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

// TODO(srj): gettimeofday isn't a good way to get a time for
// profiling-ish purposes, since that can change the whim of
// a remote time server; clock_gettime() is now recommended, but it doesn't
// exist on OSX.
WEAK int64_t halide_current_time_usec() {
    timeval now = {0,0};
    gettimeofday(&now, NULL);
    int64_t delta = (now.tv_sec - halide_reference_clock.tv_sec)*1000000;
    delta += (now.tv_usec - halide_reference_clock.tv_usec);
    return delta;
}

WEAK int halide_current_time() {
  return int(halide_current_time_usec() / 1000);
}

}
