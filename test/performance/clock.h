// A currentTime function for use in the tests.
// Returns time in milliseconds.

#ifdef _WIN32
extern "C" bool __stdcall QueryPerformanceCounter(uint64_t *);
extern "C" bool __stdcall QueryPerformanceFrequency(uint64_t *);
double current_time() {
    uint64_t t, freq;
    QueryPerformanceCounter(&t);
    QueryPerformanceFrequency(&freq);
    return (t * 1000.0) / freq;
}
#else
#include <sys/time.h>
double current_time() {
    static bool first_call = true;
    static timeval reference_time;
    if (first_call) {
        first_call = false;
        gettimeofday(&reference_time, NULL);
        return 0.0;
    } else {
        timeval t;
        gettimeofday(&t, NULL);
        return ((t.tv_sec - reference_time.tv_sec)*1000.0 +
                (t.tv_usec - reference_time.tv_usec)/1000.0);
    }
}
#endif
