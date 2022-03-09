#include "HalideRuntime.h"
#include "posix_timeval.h"

struct itimerval {
    struct timeval it_interval;
    struct timeval it_value;
};

typedef void (*sighandler_t)(int);
extern "C" sighandler_t signal(int signum, sighandler_t handler);
extern "C" int setitimer(int which, const struct itimerval *new_value, struct itimerval *old_value);

typedef unsigned long sigset_t;
extern "C" int sigprocmask(int how, const sigset_t *set, sigset_t *oldset);

#ifndef SIG_BLOCK
#define SIG_BLOCK 0
#endif
#ifndef SIG_UNBLOCK
#define SIG_UNBLOCK 1
#endif

#ifndef SIGPROF
#define SIGPROF 27
#endif

namespace {

bool inited = false;

void profiler_handler(int sig) {
    halide_profiler_state *s = halide_profiler_get_state();
    static uint64_t prev_time = 0;
    int sleep = halide_profiler_sample(s, &prev_time);
    if (sleep == -1) {
        itimerval timer_state;
        timer_state.it_interval.tv_sec = 0;
        timer_state.it_interval.tv_usec = 0;

        setitimer(2 /* ITIMER_PROF */, &timer_state, nullptr);
        signal(SIGPROF, nullptr);
        inited = false;
    }
}

}  // namespace

WEAK extern "C" void halide_start_timer_chain() {
    if (!inited) {
        halide_profiler_state *s = halide_profiler_get_state();
        itimerval timer_state;
        timer_state.it_interval.tv_sec = 0;
        timer_state.it_interval.tv_usec = s->sleep_time * 1000.0;
        timer_state.it_value = timer_state.it_interval;

        signal(SIGPROF, &profiler_handler);
        setitimer(2 /*ITIMER_PROF*/, &timer_state, nullptr);
        halide_enable_timer_interrupt();
        inited = true;
    }
}

WEAK extern "C" void halide_disable_timer_interrupt() {
    sigset_t mask = 1 << SIGPROF;
    sigprocmask(SIG_BLOCK, &mask, nullptr);
}

WEAK extern "C" void halide_enable_timer_interrupt() {
    sigset_t mask = 1 << SIGPROF;
    sigprocmask(SIG_UNBLOCK, &mask, nullptr);
}
