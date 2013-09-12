#include <stdint.h>

#ifndef __clockid_t_defined
#define __clockid_t_defined 1

typedef int32_t clockid_t;

#define CLOCK_REALTIME               0
#define CLOCK_MONOTONIC              1
#define CLOCK_PROCESS_CPUTIME_ID     2
#define CLOCK_THREAD_CPUTIME_ID      3
#define CLOCK_MONOTONIC_RAW          4
#define CLOCK_REALTIME_COARSE        5
#define CLOCK_MONOTONIC_COARSE       6
#define CLOCK_BOOTTIME               7
#define CLOCK_REALTIME_ALARM         8
#define CLOCK_BOOTTIME_ALARM         9

#endif  // __clockid_t_defined

#ifndef _STRUCT_TIMESPEC
#define _STRUCT_TIMESPEC

struct timespec {
    long tv_sec;            /* Seconds.  */
    long tv_nsec;           /* Nanoseconds.  */
};

#endif  // _STRUCT_TIMESPEC

extern "C" {

extern int clock_gettime(clockid_t clk_id, struct timespec *tp);

WEAK bool halide_reference_clock_inited = false;
WEAK timespec halide_reference_clock;

WEAK int halide_start_clock() {
    // Guard against multiple calls
    if (!halide_reference_clock_inited) {
      clock_gettime(CLOCK_REALTIME, &halide_reference_clock);
      halide_reference_clock_inited = true;
    }
    return 0;
}

WEAK int64_t halide_current_time_ns() {
    timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    int64_t delta = (now.tv_sec - halide_reference_clock.tv_sec)*1000000000;
    delta += (now.tv_nsec - halide_reference_clock.tv_nsec);
    return delta;
}

}
