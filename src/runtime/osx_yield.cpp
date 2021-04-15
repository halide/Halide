#include "runtime_internal.h"

extern "C" int swtch_pri(int);

extern "C" WEAK void halide_thread_yield() {
    swtch_pri(0);
}
