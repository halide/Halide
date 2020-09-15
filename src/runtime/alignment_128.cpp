#include "runtime_internal.h"

extern "C" WEAK_INLINE int halide_malloc_alignment() {
    return 128;
}
