#include <stdint.h>

extern "C" {

#ifdef _LP64
struct timeval {
    uint64_t tv_sec, tv_usec;
};
#else
struct timeval {
    uint32_t tv_sec, tv_usec;
};
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
    return
        (now.tv_sec - halide_reference_clock.tv_sec)*1000 + 
        (now.tv_usec - halide_reference_clock.tv_usec)/1000;
}

}
