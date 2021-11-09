#include "HalideRuntime.h"
#include "runtime_internal.h"

#ifndef __clockid_t_defined
#define __clockid_t_defined 1

typedef int32_t clockid_t;

#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1
#define CLOCK_PROCESS_CPUTIME_ID 2
#define CLOCK_THREAD_CPUTIME_ID 3
#define CLOCK_MONOTONIC_RAW 4
#define CLOCK_REALTIME_COARSE 5
#define CLOCK_MONOTONIC_COARSE 6
#define CLOCK_BOOTTIME 7
#define CLOCK_REALTIME_ALARM 8
#define CLOCK_BOOTTIME_ALARM 9

#endif  // __clockid_t_defined

#ifndef _STRUCT_TIMESPEC
#define _STRUCT_TIMESPEC

struct timespec {
    long tv_sec;  /* Seconds.  */
    long tv_nsec; /* Nanoseconds.  */
};

#endif  // _STRUCT_TIMESPEC

extern "C" {

WEAK bool halide_reference_clock_inited = false;
WEAK timespec halide_reference_clock;

// The syscall number for gettime varies across platforms:
// -- android arm is 263
// -- i386 and android x86 is 265
// -- x64 is 228

#ifndef SYS_CLOCK_GETTIME

#ifdef BITS_64
#define SYS_CLOCK_GETTIME 228
#endif

#ifdef BITS_32
#define SYS_CLOCK_GETTIME 265
#endif

#endif

extern int syscall(int num, ...);

WEAK int halide_start_clock(void *user_context) {
    // Guard against multiple calls
    if (!halide_reference_clock_inited) {
        syscall(SYS_CLOCK_GETTIME, CLOCK_REALTIME, &halide_reference_clock);
        halide_reference_clock_inited = true;
    }
    return 0;
}

WEAK int64_t halide_current_time_ns(void *user_context) {
    // It is an error to call halide_current_time_ns() if halide_start_clock() has never been called
    halide_debug_assert(user_context, halide_reference_clock_inited);

    timespec now;
    // To avoid requiring people to link -lrt, we just make the syscall directly.

    syscall(SYS_CLOCK_GETTIME, CLOCK_REALTIME, &now);
    int64_t d = int64_t(now.tv_sec - halide_reference_clock.tv_sec) * 1000000000;
    int64_t nd = (now.tv_nsec - halide_reference_clock.tv_nsec);
    return d + nd;
}

extern int usleep(int);
WEAK void halide_sleep_ms(void *user_context, int ms) {
    usleep(ms * 1000);
}
}
