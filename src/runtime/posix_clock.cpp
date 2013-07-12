#include <stdint.h>

#if defined(_WIN32)
  #include <time.h>
#else
  #include <sys/time.h>
#endif

extern "C" {

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
