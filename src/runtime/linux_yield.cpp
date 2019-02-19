#include "runtime_internal.h"

extern "C" int sched_yield();

namespace Halide { namespace Runtime { namespace Internal {

WEAK void halide_thread_yield() {
    sched_yield();
}

}}}
