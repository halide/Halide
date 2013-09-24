#include <stdint.h>
#include <unistd.h>

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

WEAK bool halide_reference_clock_inited = false;
WEAK timespec halide_reference_clock;

// The syscall number for gettime varies across platforms:
// -- android arm is 263
// -- i386 and android x86 is 265
// -- x64 is 228
// Unfortunately, we don't (yet) have a good general infrastructure
// to tell the runtime the platform we are building for (the target);
// if we use (e.g.) ifdef __arm__, that sniffs the host compiler,
// not the target. As a stopgap for now, we require the includer of this
// file (which happens to know the proper platform) to define the proper
// value.
#ifndef LINUX_CLOCK_SYSCALL_SYS_CLOCK_GETTIME
#error "LINUX_CLOCK_SYSCALL_SYS_CLOCK_GETTIME must be defined to use this file"
#endif

WEAK int halide_start_clock() {
    // Guard against multiple calls
    if (!halide_reference_clock_inited) {
        syscall(LINUX_CLOCK_SYSCALL_SYS_CLOCK_GETTIME, CLOCK_REALTIME, &halide_reference_clock);
        halide_reference_clock_inited = true;
    }
    return 0;
}

WEAK int64_t halide_current_time_ns() {
    timespec now;
    // To avoid requiring people to link -lrt, we just make the syscall directly.
    syscall(LINUX_CLOCK_SYSCALL_SYS_CLOCK_GETTIME, CLOCK_REALTIME, &now);
    int64_t d = int64_t(now.tv_sec - halide_reference_clock.tv_sec)*1000000000;
    int64_t nd = (now.tv_nsec - halide_reference_clock.tv_nsec);
    return d + nd;
}

}
