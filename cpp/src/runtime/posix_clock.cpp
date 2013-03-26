#include <stdint.h>

extern "C" {

#ifndef _STRUCT_TIMEVAL
#define _STRUCT_TIMEVAL
#ifdef _LP64
struct timeval {
    int64_t tv_sec, tv_usec;
};
#else
struct timeval {
    int32_t tv_sec, tv_usec;
};
#endif
#endif
    
extern int gettimeofday(timeval *tv, void *);

WEAK timeval halide_reference_clock;

WEAK int halide_start_clock() {
    gettimeofday(&halide_reference_clock, NULL);
    return 0;
}

WEAK int halide_current_time() {
    timeval now;
    gettimeofday(&now, NULL);    
    int delta = (now.tv_sec - halide_reference_clock.tv_sec)*1000;
    delta += (now.tv_usec - halide_reference_clock.tv_usec)/1000;
    return delta;
}

}
