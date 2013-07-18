#include <stdint.h>

#if defined(_WIN32)
  #include <time.h>
#else
  #include <sys/time.h>
#endif

extern "C" {

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

WEAK int halide_current_time() {
    timeval now;
    gettimeofday(&now, NULL);
    int delta = (now.tv_sec - halide_reference_clock.tv_sec)*1000;
    delta += (now.tv_usec - halide_reference_clock.tv_usec)/1000;
    return delta;
}

// TODO(srj): gettimeofday isn't a good way to get a time for
// profiling-ish purposes, since that can change the whim of
// a remote time server; clock_gettime() is now recommended
WEAK int64_t halide_current_time_usec() {
    timeval now;
    gettimeofday(&now, NULL);
    int64_t delta = (now.tv_sec - halide_reference_clock.tv_sec)*1000000;
    delta += (now.tv_usec - halide_reference_clock.tv_usec);
    return delta;
}

}
