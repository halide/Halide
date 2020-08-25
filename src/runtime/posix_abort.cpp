#include "runtime_internal.h"

extern "C" void abort();

extern "C" WEAK_INLINE void halide_abort() {
    abort();
}
