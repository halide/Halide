#include "runtime_internal.h"

// TODO: what should we use here???
extern "C" int sched_yield();

extern "C" WEAK void halide_thread_yield() {
    sched_yield();
}
