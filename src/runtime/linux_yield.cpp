#include "runtime_internal.h"

extern "C" int sched_yield();

extern "C" WEAK void halide_thread_yield() {
    sched_yield();
}
